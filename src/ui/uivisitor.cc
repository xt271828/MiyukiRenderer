#include <ui/uivisitor.h>
#include <ui/mykui.h>
#include <utils/log.h>
#include <ui/mainwindow.h>
#include <core/integrators/ao.h>
#include <core/integrators/pt.h>

#include <core/materials/diffusematerial.h>
#include <core/materials/glossymaterial.h>
#include <core/materials/mixedmaterial.h>
#include <core/materials/transparentmaterial.h>
#include <core/medium/homogeneous.h>
#include <core/bsdfs/microfacet.hpp>
#include <core/lights/infinite.h>

namespace Miyuki {
	namespace GUI {
		boost::signals2::signal<void(void)> uiInputChanged;
		UIVisitor::~UIVisitor() {
			connection.disconnect();
		}

		template<class T>
		std::optional<T> GetInputWithSignal(const std::string& prompt, const T& initial) {
			if (auto r = GetInput(prompt, static_cast<T>(initial))) {
				uiInputChanged();
				return r;
			}
			return {};
		}

		struct TypeSelector {
			std::unordered_map<std::string, const Reflection::TypeInfo*>_map;
			std::unordered_map<const Reflection::TypeInfo*, std::string>_invmap;
			std::unordered_map<const Reflection::TypeInfo*, std::function<Box<Reflective>(void)>> _ctors;
			std::vector<std::string> _list;
			template<class T>
			TypeSelector& option(const std::string& s) {
				auto c = T::type();

				_map[s] = c;
				_invmap[c] = s;
				_list.push_back(s);
				_ctors[c] = c->ctor;
				return *this;
			}

			TypeSelector& option(const Reflection::TypeInfo* info, const std::string& s) {
				_map[s] = info;
				_invmap[info] = s;
				_list.push_back(s);
				_ctors[info] = [=]() {return Box<Reflective>(info->ctor()); };
				return *this;
			}
			template<class Interface>
			void loadImpl() {
				const auto& impls = Reflection::getImplementations<Interface>();
				for (auto& i : impls) {
					option(i, i->name());
				}
			}
			template<class T>
			std::optional<Box<T>> select(const std::string& label, const T* current) {
				std::optional<Box<T>> opt = {};
				auto itemName = !current ? "Empty" : _invmap.at(current->typeInfo());
				Combo().name(label).item(itemName).with(true, [=, &opt]()
				{
					SingleSelectableText().name("Empty").selected(current == nullptr).with(true, [=, &opt]() {
						if (current) {
							opt = { nullptr };
						}
						if (!current) {
							ImGui::SetItemDefaultFocus();
						}
					}).show();

					for (auto ty : _list) {
						auto currentTy = current ? current->typeInfo() : nullptr;
						bool is_selected = _map.at(ty) == currentTy;
						SingleSelectableText().name(ty).selected(is_selected).with(true, [=, &opt]() {
							if (currentTy != _map.at(ty)) {
								opt = std::move(
									Reflection::static_unique_ptr_cast<T>(
										_ctors[_map.at(ty)]()));
							}
						}).show();
						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
				}).show();
				return opt;
			}
		};

		std::optional<Box<Core::Material>> selectMaterial(Core::Material* material, const std::string& label) {
			static TypeSelector selector;
			static std::once_flag flag;
			std::call_once(flag, [&]() {
				selector.loadImpl<Core::Material>();
			});
			return selector.select<Core::Material>(label, material);
		}

		std::optional<Box<Core::Shader>> selectShader(Core::Shader* shader, const std::string& label) {
			static TypeSelector selector;
			static std::once_flag flag;
			std::call_once(flag, [&]() {
				selector.loadImpl<Core::Shader>();
			});
			return selector.select<Core::Shader>(label, shader);
		}
		std::optional<Box<Core::Medium>> selectMedium(Core::Medium* m, const std::string& label) {
			static TypeSelector selector;
			static std::once_flag flag;
			std::call_once(flag, [&]() {
				selector.loadImpl<Core::Medium>();
			});
			return selector.select<Core::Medium>(label, m);
		}
		void resolutionSelector(Point2i& dim) {
			auto res2str = [](const Point2i& dim) ->const char* {
				if (dim[0] == 2560 && dim[1] == 1440) {
					return "1440p";
				}
				if (dim[0] == 1920 && dim[1] == 1080) {
					return "1080p";
				}
				if (dim[0] == 1280 && dim[1] == 720) {
					return "720p";
				}
				if (dim[0] == 858 && dim[1] == 480) {
					return "480p";
				}
				return "custom";
			};
			static const Point2i resolutions[] = {
				{2560, 1440},  {1920,1080},{1280,720},{858, 480}
			};
			Combo().name("resolution").item(res2str(dim)).with(true, [&]()
			{
				for (auto& res : resolutions) {
					bool is_selected = res == dim;
					SingleSelectableText().name(res2str(res)).selected(is_selected).with(true, [&]() {
						uiInputChanged();
						dim = res;
					}).show();
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				bool is_selected = std::string("custom") == res2str(dim);
				SingleSelectableText().name("custom").selected(is_selected)
					.with(true, [&]() {
				});
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}).show();
		}
		std::optional<Core::MicrofacetType> microfacetModelSelector(Core::MicrofacetType type) {
			static std::unordered_map<std::string, Core::MicrofacetType> map = {
				{"GGX", Core::EGGX},{"Beckmann",Core::EBeckmann},{"Phong",Core::EPhong}
			};
			static std::unordered_map<Core::MicrofacetType, const char*> invmap = {
				{ Core::EGGX,"GGX"},{Core::EBeckmann,"Beckmann"},{Core::EPhong,"Phong"}
			};
			std::optional<Core::MicrofacetType> r;
			Combo().name("model").item(invmap[type]).with(true, [&]() {
				for (auto& item : map) {
					bool is_selected = type == item.second;
					SingleSelectableText().name(item.first.c_str()).selected(is_selected).with(true, [&]() {
						uiInputChanged();
						r = item.second;
					}).show();
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
			}).show();
			return r;
		}

		void UIVisitor::init() {
			connection = uiInputChanged.connect([=]() {
				modificationCounter++;
			});
			whenVisit<Core::FloatShader>([=](Core::FloatShader* shader) {
				auto value = shader->getValue();
				if (auto r = GetInputWithSignal("value", value)) {
					shader->setValue(r.value());
				}
			});
			whenVisit<Core::RGBShader>([=](Core::RGBShader* shader) {
				Spectrum value = shader->getValue();
				if (auto r = GetInputWithSignal("value", value)) {
					shader->setValue(r.value());
				}
				if (auto r = GetInputWithSignal("multiplier", shader->getMultiplier())) {
					shader->setMultiplier(r.value());
				}
			});
			whenVisit<Core::ImageTextureShader>([=](Core::ImageTextureShader* shader) {
				Text().name(shader->imageFile.path.string()).show();
				Button().name("Select").with(true, [=]() {
					auto filename = IO::GetOpenFileNameWithDialog("Image\0 *.png;*.jpg;*.hdr\0Any File\0 * .*");
					if (!filename.empty()) {
						shader->imageFile = File(filename);
					}
				}).show();
			});
			whenVisit<Core::ScaledShader>([=](Core::ScaledShader* shader) {
				visitShaderAndSelect(shader->scale, "scale");
				visitShaderAndSelect(shader->shader, "shader");
			});
			whenVisit<Core::MixedShader>([=](Core::MixedShader* shader) {
				visitShaderAndSelect(shader->fraction, "fraction");
				visitShaderAndSelect(shader->shaderA, "shader A");
				Separator().show();
				visitShaderAndSelect(shader->shaderB, "shader  B");
				Separator().show();
			});
			whenVisit<Core::TransparentMaterial>([=](Core::TransparentMaterial* node) {
				visitShaderAndSelect(node->color, "color");
			});
			whenVisit<Core::GlossyMaterial>([=](Core::GlossyMaterial* node) {
				if (auto r = microfacetModelSelector((Core::MicrofacetType)node->model)) {
					node->model = r.value();
				}
				visitShaderAndSelect(node->color, "color");
				visitShaderAndSelect(node->roughness, "roughness");
			});
			whenVisit<Core::DiffuseMaterial>([=](Core::DiffuseMaterial* node) {
				visitShaderAndSelect(node->color, "color");
				visitShaderAndSelect(node->roughness, "roughness");
			});
			whenVisit<Core::MixedMaterial>([=](Core::MixedMaterial* node) {
				visitShaderAndSelect(node->fraction, "fraction");
				visitMaterialAndSelect(node->matA, "material A");
				Separator().show();
				visitMaterialAndSelect(node->matB, "material B");
				Separator().show();
			});
			whenVisit<Core::MeshFile>([=](Core::MeshFile* node) {
				auto name = node->name;
				if (auto r = GetInputWithSignal("name", name)) {
					node->name = r.value();
				}
				if (auto r = GetInputWithSignal("transform", node->transform)) {
					node->transform = r.value();
				}
			});
			whenVisit<Core::Object>([=](Core::Object* node) {
				auto graph = engine->getGraph();
				auto objectName = node->alias;
				auto matName = node->material->name;
				if (node->alias.empty()) {
					node->alias = node->name;
				}
				if (auto r = GetInputWithSignal("name", objectName)) {
					node->alias = r.value();
				}
				LineText("material");

				Combo().name("use material").item(matName).with(true, [=]()
				{
					for (auto& slot : graph->materials) {
						auto& m = slot->material;
						bool is_selected = (node->material && m.get() == node->material->material.get());
						SingleSelectableText().name(slot->name).selected(is_selected).with(true, [=, &m, &slot]() {
							node->material = slot.get();
						}).show();
						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
				}).show();

				visit(node->material);
			});
			whenVisit<Core::MaterialSlot>([=](Core::MaterialSlot* slot) {
				if (auto r = GetInputWithSignal("name", slot->name)) {
					slot->name = r.value();
				}
				if (slot->material) {
					auto m = modificationCounter;
					visitShaderAndSelect(slot->material->emission, "emission");
					if (modificationCounter != m) {
						engine->getGraph()->lights->notifyChange();
					}
					visitShaderAndSelect(slot->material->normalMap, "normal");
				}
				visitMaterialAndSelect(slot->material, "material");
				visitMediumAndSelect(slot->medium, "volume");
			});
			whenVisit<Core::WorldConfig>([=](Core::WorldConfig* world) {
				if (auto r = GetInputWithSignal("ray bias", world->rayBias)) {
					world->rayBias = r.value();
				}
				if (!world->environmentMap) {
					world->environmentMap = makeBox<Core::InfiniteAreaLight>();
				}
				visit(world->environmentMap);
			});
			whenVisit<Core::InfiniteAreaLight>([=](Core::InfiniteAreaLight* light) {
				visitShaderAndSelect(light->shader, "shader");
				if (auto r = GetInputWithSignal("rotation", light->rotation)) {
					light->rotation = r.value();
				}
			});
			whenVisit<Core::PerspectiveCamera>([=](Core::PerspectiveCamera* camera) {
				if (auto r = GetInputWithSignal("viewpoint", camera->viewpoint)) {
					camera->viewpoint = r.value();
				}
				if (auto r = GetInputWithSignal("direction", RadiansToDegrees(camera->direction))) {
					camera->direction = DegreesToRadians(r.value());
				}
				if (auto r = GetInputWithSignal("fov", RadiansToDegrees(camera->fov))) {
					camera->fov = DegreesToRadians(r.value());
				}
				if (auto r = GetInputWithSignal("lensRadius", camera->lensRadius)) {
					camera->lensRadius = r.value();
				}
				if (auto r = GetInputWithSignal("focal distance", camera->focalDistance)) {
					camera->focalDistance = r.value();
				}
			});
			whenVisit<Core::AOIntegrator>([=](Core::AOIntegrator* integrator) {
				if (auto r = GetInputWithSignal("samples", integrator->spp)) {
					integrator->spp = r.value();
				}
				if (auto r = GetInputWithSignal("occlusion distance", integrator->occlusionDistance)) {
					integrator->occlusionDistance = r.value();
				}
			});
			whenVisit<Core::PathTracerIntegrator>([=](Core::PathTracerIntegrator* integrator) {
				if (auto r = GetInputWithSignal("samples", integrator->spp)) {
					integrator->spp = r.value();
				}
				if (auto r = GetInputWithSignal("min depth", integrator->minDepth)) {
					integrator->minDepth = r.value();
				}
				if (auto r = GetInputWithSignal("max depth", integrator->maxDepth)) {
					integrator->maxDepth = r.value();
				}
				if (auto r = GetInputWithSignal("ray clamp", integrator->maxRayIntensity)) {
					integrator->maxRayIntensity = r.value();
				}
				if (auto r = GetInputWithSignal("use NEE", integrator->useNEE)) {
					integrator->useNEE = r.value();
				}
				if (auto r = GetInputWithSignal("denoised", integrator->denoised)) {
					integrator->denoised = r.value();
				}
			});
			whenVisit<Core::FilmConfig>([=](Core::FilmConfig* config) {
				if (auto r = GetInputWithSignal("scale", 100 * config->scale)) {
					config->scale = r.value() / 100.0f;
				}
				resolutionSelector(config->dimension);
				if (auto r = GetInputWithSignal("dimension", config->dimension)) {
					config->dimension = r.value();
				}
				Separator().show();
				Text().name("output image").show();
				Text().name(config->outputImage.path.string()).show();
				if (config->outputImage.str().empty()) {
					config->outputImage = File(cxx::filesystem::path("out.png"));
				}
				Button().name("Select").with(true, [=]() {
					auto filename = IO::GetSaveFileNameWithDialog("Image\0 *.png");
					if (!filename.empty()) {
						config->outputImage = File(filename);
					}
				}).show();
			});

			whenVisit<Core::HomogeneousMedium>([](Core::HomogeneousMedium* medium) {
				if (auto r = GetInputWithSignal("g", medium->g)) {
					medium->g = r.value();
				}
				if (auto r = GetInputWithSignal("absorption", medium->sigma_a)) {
					medium->sigma_a = r.value();
				}
				if (auto r = GetInputWithSignal("scattering", medium->sigma_s)) {
					medium->sigma_s = r.value();
				}
			});
		}

		void UIVisitor::visitMaterialAndSelect(Box<Core::Material>& material, const std::string& label) {
			TreeNode().name(std::string("").append(label).append("##00")).with(true, [=, &material]() {
				if (auto r = selectMaterial(material.get(), label)) {
					material = std::move(r.value());
				}
				visit(material);
			}).flag(ImGuiTreeNodeFlags_DefaultOpen).show();
		}


		void UIVisitor::visitShaderAndSelect(Box<Core::Shader>& shader, const std::string& label) {
			TreeNode().name(std::string("").append(label).append("##00")).with(true, [=, &shader]() {
				if (auto r = selectShader(shader.get(), label)) {
					shader = std::move(r.value());
				}
				visit(shader);
			}).flag(ImGuiTreeNodeFlags_DefaultOpen).show();
		}
		void UIVisitor::visitMediumAndSelect(Box<Core::Medium>& medium, const std::string& label) {
			TreeNode().name(std::string("").append(label).append("##00")).with(true, [=, &medium]() {
				if (auto r = selectMedium(medium.get(), label)) {
					medium = std::move(r.value());
				}
				visit(medium);
			}).flag(ImGuiTreeNodeFlags_DefaultOpen).show();
		}
		void UIVisitor::visitSelected() {
			if (selected) {
				visit(selected);
			}
		}
		void UIVisitor::visitWorld() {
			auto graph = engine->getGraph();
			if (!graph)return;
			visit(&graph->worldConfig);
		}
		void UIVisitor::visitGraph() {
			auto graph = engine->getGraph();
			if (!graph)return;
			TreeNode().name("Materials").with(true, [=]() {
				auto& materials = graph->materials;
				int index = 0;
				for (auto& slot : materials) {
					auto& material = slot->material;
					const auto& name = slot->name;
					SingleSelectableText().name(name).selected(material.get() == selected)
						.with(true, [=, &material, &slot]() {
						selected = slot.get();
						selectedNodeType = kMaterial;
						selectedMaterialIndex = index;
					}).show();
					index++;
				}
			}).show();

			TreeNode().name("Meshes").with(true, [=]() {
				auto& meshes = graph->meshes;
				for (auto& mesh : meshes) {
					auto name = mesh->name;
					int flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
					if (mesh.get() == selected)
						flags |= ImGuiTreeNodeFlags_Selected;
					TreeNode().name(name).flag(flags).with(true, [=, &mesh]() {
						if (ImGui::IsItemClicked()) {
							selected = mesh.get();
							selectedNodeType = kMesh;
						}
						for (auto& object : mesh->objects) {
							if (!object)
								continue;
							SingleSelectableText().name(object->name)
								.selected(object.get() == selected)
								.with(true, [=, &object]() {
								selected = object.get();
								selectedNodeType = kObject;
							}).show();
						}
					}).with(false, [=, &mesh]() {
						if (ImGui::IsItemClicked()) {
							selected = mesh.get();
							selectedNodeType = kMesh;
						}
					}).show();
				}
			}).show();
		}
		void UIVisitor::visitCamera() {
			auto graph = engine->getGraph();
			if (!graph)return;
			visit(graph->activeCamera);
		}

		std::optional<Box<Core::Integrator>> selectIntegrator(Core::Integrator* integrator, const std::string& label) {
			static TypeSelector selector;
			static std::once_flag flag;
			std::call_once(flag, [&]() {
				selector.loadImpl<Core::Integrator>();
			});
			return selector.select<Core::Integrator>(label, integrator);
		}


		void UIVisitor::visitIntegrator() {
			auto graph = engine->getGraph();
			if (!graph)return;
			auto& integrator = graph->integrator;
			ImGui::PushID(&integrator);
			if (auto r = selectIntegrator(integrator.get(), "integrator")) {
				integrator = std::move(r.value());
			}
			visit(integrator);
			ImGui::PopID();

			static TypeSelector selector;
			static std::once_flag flag;
			std::call_once(flag, [&]() {
				selector.loadImpl<Core::Sampler>();
			});

			ImGui::PushID("sampler");
			if (auto r = selector.select<Core::Sampler>("sampler", graph->sampler.get())) {
				graph->sampler = std::move(r.value());
			}
			ImGui::PopID();
		}
		void UIVisitor::visitFilm() {
			auto graph = engine->getGraph();
			if (!graph)return;
			visit(&graph->filmConfig);
		}

		void UIVisitor::loadWindowView(Arc<Core::Film>& film) {
			window.loadView(film);
		}
		void UIVisitor::startInteractive() {
			Core::ProgressiveRenderCallback cb = [=](Arc<Core::Film> film) {
				std::unique_lock<std::mutex> lock(renderCBMutex, std::try_to_lock);
				if (lock.owns_lock())
					loadWindowView(film);
			};
			std::thread th([=]() {
				try {
					window.openModal("Starting rendering", []() {});
					if (!engine->startProgressiveRender(cb)) {
						window.showErrorModal("Error",
							"Cannot start rendering: no integrator or integrator does not support interactive\n");
					}
					window.closeModal();
				}
				catch (std::exception& e) {
					Log::log("error: {}\n", e.what());
					window.closeModal();
				}

			});
			th.detach();
		}
		void UIVisitor::stopRender() {
			engine->requestAbortRender();
		}
	}
}