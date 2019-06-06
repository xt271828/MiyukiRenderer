#ifndef MIYUKI_LEAF_H
#define MIYUKI_LEAF_H

#include <graph/node.h>
#include <math/transform.h>
#include <io/image.h>

// Some Leaf Node Types
namespace Miyuki {
	namespace Graph {
		enum LeafType {
			kNull,
			kInt,
			kFloat,
			kFloat3,
			kTransform,
			kString,
			kImage
		};

		template<typename T>
		struct _GetLeafType {
			static constexpr int Type = kNull;
		};

		template<>
		struct _GetLeafType<Float> {
			static constexpr int Type = kFloat;
		};

		template<>
		struct _GetLeafType<Vec3f> {
			static constexpr int Type = kFloat3;
		};

		template<>
		struct _GetLeafType<int> {
			static constexpr int Type = kInt;
		};

		template<>
		struct _GetLeafType<Transform> {
			static constexpr int Type = kTransform;
		};

		template<>
		struct _GetLeafType<std::string> {
			static constexpr int Type = kString;
		};

		template<>
		struct _GetLeafType<IO::ImagePtr> {
			static constexpr int Type = kImage;
		};

		inline const char* LeafTypeToString(LeafType t) {
			switch (t) {
			case kImage:
				return "Image";
			case kString:
				return "String";
			case kFloat:
				return "Float";
			case kInt:
				return "Int";
			case kFloat3:
				return "Float3";
			case kTransform:
				return "Tranform";
			}
			return nullptr;
		}

		class BasicLeafNode : public Node {
		public:
			virtual const LeafType leafType() const = 0;
			BasicLeafNode(const std::string& n, Graph* g) :Node(n, g) {}
			const std::vector<Edge*> subnodes()const override{ return {}; }
		};

		template<typename T>
		class LeafNode : public BasicLeafNode {
			T value;
		public:
			LeafNode(const T& value, Graph* graph = nullptr) :
				BasicLeafNode("", graph), value(value) {}
			LeafNode(const std::string& name, const T& value, Graph* graph = nullptr) :
				BasicLeafNode(name, graph), value(value) {}
			virtual const LeafType leafType() const override {
				return LeafType(_GetLeafType<T>::Type);
			}
			virtual const char* type()const {
				return LeafTypeToString(leafType());
			}
			bool isLeaf()const override final { return true; }
			virtual void serialize(json& j)const override {
				Node::serialize(j);
				j["value"] = value;
			}
			const T& getValue()const {
				return value;
			}
			void setValue(const T& v) { value = v; }
		};

		template<typename T>
		struct LeafNodeDeserializer : public IDeserializer {
			virtual Node* deserialize(const json& j, Graph* G) {
				// skip type checking
				return new LeafNode<T>(j.at("name").get<std::string>(), j.at("value").get<T>(), G);
			}
		};


		using IntNode = LeafNode<int>;
		using FloatNode = LeafNode<Float>;
		using Float3Node = LeafNode<Vec3f>;
		using StringNode = LeafNode<std::string>;
		using TranformNode = LeafNode<Transform>;
		using ImageNode = LeafNode<IO::ImagePtr>;

		template<typename T>
		struct _ConvertToLeafType {
			using type = typename  std::conditional<_GetLeafType<T>::Type == kNull, T, LeafNode<T>>::type;
		}; 

#define MYK_DECL_MEMBER(Type, Name)  EdgeT<_ConvertToLeafType<Type>::type> Name
	}
}
#endif