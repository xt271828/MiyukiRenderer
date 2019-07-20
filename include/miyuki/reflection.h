#ifndef MIYUKI_REFLECTION_H
#define MIYUKI_REFLECTION_H

#include <miyuki.h>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/algorithm/string/replace.hpp>


#define MYK_REFL_NIL (__nil)
#define _MYK_REFL_NIL __nil
namespace Miyuki {
	namespace Reflection {
		struct Nil {};
		template<class Visitor>
		void accept(const Nil&, Visitor) {}
		template<class Visitor>
		void accept(Nil&, Visitor) {}
		struct Component;
		struct Deleter {
			std::function<void(Component*)> deleter;
			Deleter() {}
			Deleter(std::function<void(Component*)> f) :deleter(std::move(f)) {}
			void operator()(Component* t) {
				deleter(t);
			}
		};
		template<class T = Component>
		using Box = std::unique_ptr<T, Deleter>;

		template<class T, class... Args>
		Box<T> make_box(Args... args) {
			return Box<T>(new T(args...), Deleter([](Component* p) {delete p; }));
		}

		class OutObjectStream {
			json data;
			struct State {
				std::set<const Component*> visited;
			};
			std::shared_ptr<State> state;
		public:
			OutObjectStream() {
				state = std::make_shared<State>();
			}
			OutObjectStream sub() {
				OutObjectStream stream;
				stream.state = state;
				return stream;
			}
			void write(const std::string& key, std::nullptr_t) {
				data[key] = {};
			}
			void write(const std::string& key, const OutObjectStream& stream) {
				data[key] = stream.data;
			}
			void write(const std::string& key, const std::string& value) {
				data[key] = value;
			}
			void write(const std::string& key, size_t value) {
				data[key] = value;
			}
			void write(std::nullptr_t) {
				data = {};
			}
			void append(const OutObjectStream& stream) {
				data.push_back(stream.data);
			}
			void append(std::nullptr_t) {
				data.push_back(json{});
			}
			bool hasSerialized(const Component* object)const {
				return state->visited.find(object) != state->visited.end();
			}
			void addSerialized(const Component* object) {
				state->visited.insert(object);
			}
			template<class T>
			void write(const T& value) {
				data = value;
			}
			
			const json& toJson()const { return data; }
		};

		struct OutStreamVisitor {
			OutObjectStream& stream;
			OutStreamVisitor(OutObjectStream& stream) :stream(stream) { }
			template<class T>
			void visit(const T& value, const char* name) {
				auto sub = stream.sub();
				save(value, sub);
				stream.write(name, sub);
			}
		};

		template<class T>
		std::enable_if_t<std::is_base_of_v<Component, T>, void> save(const T& value, OutObjectStream& stream) {
			auto sub = stream.sub();
			OutStreamVisitor visitor(sub);
			Miyuki::Reflection::visit(value, visitor);

			stream.write("type", T::type()->name());
			stream.write("val", sub);
		}


		template<class T>
		std::enable_if_t<std::is_base_of_v<Component, T>, void> save(const T* value, OutObjectStream& stream) {
			if (value == nullptr) {
				stream.write(nullptr);
			}
			else {
				stream.write("meta", "ref");
				stream.write("val", reinterpret_cast<uint64_t>(value));
			}
		}

		template<class T>
		std::enable_if_t<std::is_base_of_v<Component, T>, void>
			save(const Box<T>& value, OutObjectStream& stream) {
			if (value == nullptr) {
				stream.write(nullptr);
			}
			else if (stream.hasSerialized(value.get())) {
				throw std::runtime_error("Multiple strong reference to same object");
			}
			else {
				auto s = stream.sub();
				value->serialize(s);
				stream.write("meta", "val");
				stream.write("addr", reinterpret_cast<uint64_t>(static_cast<Component*>(value.get())));
				stream.write("val", s);
				stream.addSerialized(value.get());
			}
		}

		class InObjectStream {
			friend class Runtime;
			const json& data;
			struct State {
				std::unordered_map<size_t, Component*> map;
			};
			std::shared_ptr<State> state;
		public:
			InObjectStream(const json& data, std::shared_ptr<State>s = nullptr) :data(data) {
				if (!s)
					state = std::make_shared<State>();
				else
					state = s;
			}
			InObjectStream sub(const std::string& key) {
				return InObjectStream(data.at(key), state);
			}
			InObjectStream sub(size_t index) {
				return InObjectStream(data.at(index), state);
			}
			bool contains(const std::string& key) {
				return data.contains(key);
			}
			const json& getJson() {
				return data;
			}
			bool has(size_t addr) {
				return state->map.find(addr) != state->map.end();
			}
			Component* fetchByAddr(size_t addr) {
				return state->map.at(addr);
			}
			void add(size_t addr, Component* v) {
				state->map[addr] = v;
			}
			size_t size() { return data.size(); }
		};
		template<typename Derived, typename Base, typename Del>
		inline std::unique_ptr<Derived, Del>
			static_unique_ptr_cast(std::unique_ptr<Base, Del>&& p)
		{
			auto d = dynamic_cast<Derived*>(p.release());
			return std::unique_ptr<Derived, Del>(d, std::move(p.get_deleter()));
		}

		template<typename Derived, typename Base>
		inline Box<Derived> downcast(Box<Base>&& p) {
			return static_unique_ptr_cast(p);
		}

		template<class T>
		inline std::enable_if_t<std::is_base_of_v<Component, T>, void> load(T& value, InObjectStream& stream) {
			TypeInfo* type = T::type();
			auto& data = stream.getJson();
			auto ty2 = data.at("type").get<std::string>();
			if (ty2 != type->name()) {
				throw std::runtime_error(fmt::format("Excepted type {} but froud {}", type->name(), ty2));
			}
			auto s = stream.sub("val");
			InStreamVisitor visitor(s);
			visit(value, visitor);
		}



		template<class T>
		inline std::enable_if_t<std::is_base_of_v<Component, T>, void>
			load(Box<T>& value, InObjectStream& stream) {
			auto& data = stream.getJson();
			if (data.is_null()) {
				value = nullptr; return;
			}			
			auto meta = data.at("meta").get<std::string>();
			if (meta == "ref") {
				throw std::runtime_error(fmt::format("multiple strong ref !!"));
			}
			else if (meta == "val") {
				auto val = data.at("val");
				auto type = val.at("type");
				TypeInfo* info = getTypeByName(type.get<std::string>());
				auto addr = data.at("addr").get<uint64_t>();
				value = std::move(static_unique_ptr_cast<T>(info->ctor()));
				stream.add(addr, value.get());
				auto in = stream.sub("val");
				info->loader(*value, in);
						
			}
			else {
				throw std::runtime_error(fmt::format("Unrecognized meta info: {}", meta));
			}
		}

		template<class T>
		inline std::enable_if_t<std::is_base_of_v<Component, T>, void> load(T*& value, InObjectStream& stream) {
			auto& data = stream.getJson();
			if (data.is_null()) {
				value = nullptr; return;
			}
			auto meta = data.at("meta").get<std::string>();
			if (meta == "ref") {
				auto val = data.at("val").get<uint64_t>();
				if (stream.has(val)) {
					value = static_cast<T*>(stream.fetchByAddr(val));
				}
				else {
					throw std::runtime_error(fmt::format("ref {} has not been loaded", val));
				}
			}
			else if (meta == "val") {
				throw std::runtime_error(fmt::format("weak ref !!"));
			}
			else {
				throw std::runtime_error(fmt::format("Unrecognized meta info: {}", meta));
			}
		}
		template<class T>
		inline void save(const std::vector<T>& vec, OutObjectStream& stream) {
			for (const auto& item : vec) {
				auto sub = stream.sub();
				save(item, sub);
				stream.append(sub);
			}
		}
		template<class T>
		inline void load(std::vector<T>& vec, InObjectStream& stream) {
			for (auto i = 0; i < stream.size(); i++) {
				auto sub = stream.sub(i);
				T value;
				load(value, sub);
				vec.emplace_back(std::move(value));
			}
		}

		template<class K, class V>
		inline void save(const std::unordered_map<K, V>& map, OutObjectStream& stream) {
			for (const auto& item : map) {
				auto pair = stream.sub();
				auto key = pair.sub();
				auto val = pair.sub();
				save(item.first, key);
				save(item.second, val);
				pair.write("key", key);
				pair.write("val", val);
				stream.append(pair);
			}
		}
		template<class K, class V>
		inline void load(std::unordered_map<K, V>& map, InObjectStream& stream) {
			for (auto i = 0; i < stream.size(); i++) {
				auto pair = stream.sub(i);
				K key;
				V val;
				load(key, pair.sub("key"));
				load(val, pair.sub("val"));
				map[key] = val;
			}
		}
		template<class K, class V>
		inline void save(const std::map<K, V>& map, OutObjectStream& stream) {
			for (const auto& item : map) {
				auto pair = stream.sub();
				auto key = pair.sub();
				auto val = pair.sub();
				save(item.first, key);
				save(item.second, val);
				pair.write("key", key);
				pair.write("val", val);
				stream.append(pair);
			}
		}
		template<class K, class V>
		inline void load(std::map<K, V>& map, InObjectStream& stream) {
			for (auto i = 0; i < stream.size(); i++) {
				auto pair = stream.sub(i);
				K key;
				V val;
				load(key, pair.sub("key"));
				load(val, pair.sub("val"));
				map[key] = val;
			}
		}

#define MYK_SAVE_TRIVIAL(type)\
		inline void save(const type& value, Miyuki::Reflection::OutObjectStream& stream) {\
			stream.write(value);\
		}
#define MYK_LOAD_TRIVIAL(type)\
		inline void load(type& value, Miyuki::Reflection::InObjectStream& stream) {\
			value = stream.getJson().get<type>();\
		}

#define MYK_SAVE_LOAD_TRIVIAL(type) 	MYK_SAVE_TRIVIAL(type)MYK_LOAD_TRIVIAL(type)
		MYK_SAVE_LOAD_TRIVIAL(bool);
		MYK_SAVE_LOAD_TRIVIAL(int32_t);
		MYK_SAVE_LOAD_TRIVIAL(uint32_t);
		MYK_SAVE_LOAD_TRIVIAL(uint64_t);
		MYK_SAVE_LOAD_TRIVIAL(int64_t);
		MYK_SAVE_LOAD_TRIVIAL(int8_t);
		MYK_SAVE_LOAD_TRIVIAL(char);
		MYK_SAVE_LOAD_TRIVIAL(uint8_t);
		MYK_SAVE_LOAD_TRIVIAL(Float);
		MYK_SAVE_LOAD_TRIVIAL(double);
		MYK_SAVE_LOAD_TRIVIAL(Vec3f);
		MYK_SAVE_LOAD_TRIVIAL(Spectrum);
		MYK_SAVE_LOAD_TRIVIAL(std::string);
		MYK_SAVE_LOAD_TRIVIAL(Point2i);
		MYK_SAVE_LOAD_TRIVIAL(Point2f);
		MYK_SAVE_LOAD_TRIVIAL(Point3f);

		namespace detail {
			template<class T>
			struct ZeroInit {
				static void init(T& value) { value = T(); }
			};
			template<class T>
			struct ZeroInit<T*> {
				static void init(T*& value) { value = nullptr; }
			};
		}
		struct InStreamVisitor {
			int index = 0;
			InObjectStream& stream;
			InStreamVisitor(InObjectStream& stream) :stream(stream) {}
			template<class T>
			void visit(T& value, const char* name) {
				if (stream.contains(name)) {
					auto sub = stream.sub(name);
					load(value, sub);
				}
			}
		};


		struct TypeInfo {
			const char* _name;
			using Constructor = std::function<Box<Component>(void)>;
			using Loader = std::function<void(Component&, InObjectStream&)>;
			using Saver = std::function<void(const Component&, OutObjectStream&)>;
			Constructor ctor;
			Loader loader;
			Saver saver;
			const char* name()const { return _name; }
			bool isOfName(const char* name) {
				return std::strcmp(name, _name) == 0;
			}
		};
		template<class T>
		TypeInfo* GetTypeInfo(const char* name) {
			static std::once_flag flag;
			static TypeInfo* info;
			std::call_once(flag, [&]() {
				info = new TypeInfo();
				info->_name = name;
				info->saver = [=](const Component& value, OutObjectStream& stream) {
					save(static_cast<const T&>(value), stream);
				};
				info->ctor = [=]()->Box<Component> {
					return make_box<T>();
				};
				info->loader = [=](Component& value, InObjectStream& stream) {
					load(static_cast<T&>(value), stream);
				};
			});
			return info;
		}

#define MYK_INTERFACE(Interface) static std::string interfaceInfo(){\
									return "Interface." #Interface;\
								}
		struct ComponentVisitor;
		struct Component {
			MYK_INTERFACE(Component);
			virtual TypeInfo* typeInfo() const = 0;
			virtual void serialize(OutObjectStream& stream)const {
				typeInfo()->saver(*this, stream);
			}
			virtual void deserialize(InObjectStream& stream) {
				typeInfo()->loader(*this, stream);
			}
			virtual ~Component() = default;
			inline void accept(ComponentVisitor& visitor);
		protected:
			static Nil _MYK_REFL_NIL;
		};
		struct ComponentVisitor {
			std::unordered_map<TypeInfo*, std::function<void(Component*)>>_map;
		public:
			void visit(Component* trait) {
				if (!trait)return;
				_map.at(trait->typeInfo())(trait);
			}
			template<class T>
			void visit(Box<T>& trait) {
				visit(trait.get());
			}
			template<class T>
			void visit(const std::function<void(T*)>& f) {
				_map[T::type()] = [=](Component* p) {
					f(static_cast<T*>(p));
				};
			}
		};
		namespace detail {
			template<class T = Component>
			struct Match {
				T* value;
				bool matched = false;
				Match(T* value) :value(value) {}
				template<class U, class Function>
				Match& with(Function func) {
					if (matched)return *this;
					if (value && typeof(value) == typeof<U>()) {
						auto f = std::move(func);
						f(static_cast<U*>(value));
						matched = true;
					}
					return *this;
				}
			};
		}
		template<class T = Component>
		detail::Match<T> match(T * trait) {
			return detail::Match<T>(trait);
		};
		inline void Component::accept(ComponentVisitor& visitor) {
			visitor.visit(this);
		}
		template<int>
		struct ID {};

		template<class T, class Visitor>
		void visit(T& x, Visitor& v) {
			using _T = std::decay_t<T>;
			accept(x, v);
		}
		namespace detail {
			struct TypeInfoCompare {
				bool operator ()(const TypeInfo* a, const TypeInfo* b)const {
					return std::less<std::string>()(a->name(), b->name());
				}
			};
			using ImplSet = std::set<TypeInfo*, TypeInfoCompare>;
			struct Types {
				std::set<TypeInfo*> _registerdTypes;
				std::unordered_map<std::string, TypeInfo*> _registerdTypeMap;
				std::unordered_map<std::string, ImplSet> _impls;
				static Types* all;
				static std::once_flag flag;
				static Types& get() {
					std::call_once(flag, [&]() {all = new Types(); });
					return *all;
				}
			};

		}
		template<class T>
		inline void registerType() {
			auto t = T::type();
			detail::Types::get()._registerdTypes.insert(t);
			detail::Types::get()._registerdTypeMap[t->name()] = t;
		}

		template<class Impl, class Interface>
		inline void registerImplementation() {
			auto t = Impl::type();
			auto inter = Interface::interfaceInfo();
			auto& impls = detail::Types::get()._impls;
			if (impls.find(inter) == impls.end()) {
				impls[inter] = {};
			}
			impls[inter].insert(t);
		}

		template<class Interface>
		inline const detail::ImplSet& getImplementations() {
			auto inter = Interface::interfaceInfo();
			const auto& impls = detail::Types::get()._impls;
			return impls.at(inter);
		}

		inline Box<Component> createComponent(const std::string& name) {
			auto t = detail::Types::get()._registerdTypeMap.at(name);
			return t->ctor();
		}
		inline TypeInfo* getTypeByName(const std::string& name) {
			return detail::Types::get()._registerdTypeMap.at(name);
		}
		inline TypeInfo* typeof(Component* trait) {
			if (!trait)return nullptr;
			return trait->typeInfo();
		}
		template<class T>
		inline TypeInfo* typeof() {
			return T::type();
		}

	}

	using Component = Reflection::Component;
	using Reflection::Box;
	template<class T>
	using Arc = std::shared_ptr<T>;

#define MYK_META(Type) using __Self = Type;struct Meta;\
		static inline Miyuki::Reflection::TypeInfo* type();\
		virtual Miyuki::Reflection::TypeInfo* typeInfo() const final override{\
			return __Self::type();\
		}


#define MYK_AUTO_REGSITER_TYPE(Type, Alias)\
		struct Injector_##Type{\
			Injector_##Type(){\
				Miyuki::Reflection::registerType<Type>();\
			}\
		};static Injector_##Type _injector_##Type;\
inline Miyuki::Reflection::TypeInfo* Type::type(){return Miyuki::Reflection::GetTypeInfo<Type>(Alias);}
#define MYK_IMPL(Type, Interface, Alias) MYK_AUTO_REGSITER_TYPE(Type, Alias)\
	struct Injector_##Type##_##Interface{\
		Injector_##Type##_##Interface(){\
			Miyuki::Reflection::registerImplementation<Type, Interface>();\
		}\
	};static Injector_##Type##_##Interface __injector__##Type##_##Interface;

#define MYK_BEGIN_REFL(Type) struct Type::Meta {\
						 enum {__idx = __COUNTER__}; using __Self = Type;\
						template<int i>using UID = Miyuki::Reflection::ID<i>;static constexpr char * TypeName = #Type;

#define MYK_ATTR(name)  enum {__attr_index_##name = __COUNTER__ - __idx - 1 };\
						static auto& getAttribute(__Self& self, UID<__attr_index_##name>){return self.name;} \
						static auto& getAttribute(const __Self& self, UID<__attr_index_##name>){return self.name;} \
						static auto getAttributeName(UID<__attr_index_##name>) { return #name; }
#define MYK_END_REFL(Type)  enum{__attr_count =  __COUNTER__ - __idx - 1 };\
					template<class SelfT, class Visitor>\
					static void accept(SelfT& self,Visitor& visitor){ \
						__AcceptHelper<SelfT, Visitor, __attr_count - 1>::accept(self, visitor);\
					}\
					template<class SelfT, class Visitor, int i>\
					struct __AcceptHelper {\
						static void accept(SelfT& self, Visitor& visitor) {\
						if constexpr (!std::is_same_v<std::decay_t<decltype(Meta::getAttribute(self, UID<Meta::__attr_count - i - 1>()))>,\
								Miyuki::Reflection::Nil>)\
							visitor.visit(Meta::getAttribute(self, UID<Meta::__attr_count - i - 1>()), Meta::getAttributeName(UID<Meta::__attr_count - i - 1>()));\
							__AcceptHelper<SelfT, Visitor, i - 1>::accept(self, visitor);\
						}\
					};\
					template<class SelfT, class Visitor>\
					struct __AcceptHelper<SelfT, Visitor, 0> {\
					static void accept(SelfT& self, Visitor& visitor) {\
							if constexpr (!std::is_same_v<std::decay_t<decltype(Meta::getAttribute(self, UID<Meta::__attr_count - 1>()))>,\
								Miyuki::Reflection::Nil>)\
							visitor.visit(Meta::getAttribute(self, UID<Meta::__attr_count - 1>()), Meta::getAttributeName(UID<Meta::__attr_count - 1>()));\
						}\
					};};\
					template<class Visitor>\
					void accept(Type& self, Visitor vis){\
						Type::Meta::accept(self, vis);\
					}\
					template<class Visitor>\
					void accept(const Type& self, Visitor vis){\
						Type::Meta::accept(self, vis);\
					}

#define MYK_SEQ_MACRO(r, data, elem) MYK_ATTR(elem)
#define MYK_REFL(Type, Attributes) MYK_BEGIN_REFL(Type) BOOST_PP_SEQ_FOR_EACH(MYK_SEQ_MACRO, _, Attributes) MYK_END_REFL(Type)

}

#define MYK_REFL_IMPLEMENTATION Miyuki::Reflection::detail::Types* Miyuki::Reflection::detail::Types::all;\
std::once_flag  Miyuki::Reflection::detail::Types::flag;\
Miyuki::Reflection::Nil Miyuki::Reflection::Component::  _MYK_REFL_NIL;

#endif