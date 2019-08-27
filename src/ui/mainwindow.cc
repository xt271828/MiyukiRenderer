#include <ui/mainwindow.h>
#include <ui/logwindow.h>
#include <utils/future.hpp>
#include <utils/thread.h>

static void glfw_error_callback(int error, const char* description) {
	fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

static void Draw(const Miyuki::HW::Texture& texture) {
	ImGui::Image((void*)texture.getTexture(),
		ImVec2(texture.size()[0], texture.size()[1]));

}
namespace Miyuki {
	namespace GUI {

		void MainWindow::close() {
			cxx::filesystem::current_path(programPath);
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();

			glfwDestroyWindow(window);
			glfwTerminate();

			saveConfig();

		}
		void MainWindow::loadBackGroundShader() {
			glGenBuffers(1, &vbo);
			HW::Shader vertex(
				R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 texCoord;
void main()
{
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
	texCoord = aTexCoord;
}
)", HW::kVertexShader);
			HW::Shader fragment(
				R"(
#version 330 core
in vec2 texCoord;;
out vec4 FragColor;
uniform sampler2D background;
uniform float width;
uniform float height;
uniform float dim;
void main()
{
	float x = texCoord.x, y=1.0f - texCoord.y;
	float w = textureSize(background, 0).x, h = textureSize(background, 0).y;
	y /= (width / height) / ( w / h);
	if(x > 1.0f || y > 1.0f)
		FragColor = vec4(0.0f,0.0f,0.0f,1.0f);
	else{
		FragColor = texture(background, vec2(x, y)) * (1.0f - dim);
	}
}
)", HW::kFragmentShader);
			backgroundShader = std::make_unique<HW::ShaderProgram>(std::move(vertex), std::move(fragment));
			//	Assert(backgroundShader->valid());
		}
		void MainWindow::loadBackgroundImage() {
			if (!config.contains("background-image")) {
				background = std::make_unique<HW::Texture>(4, 4);
				return;
			}
			IO::Image image(config["background-image"].get<std::string>());
			background = std::make_unique<HW::Texture>(image);
			Log::log("Loaded background image\n");
		}
		void MainWindow::drawBackground() {
			static float vertices[] = {
				// first triangle
				1.0f,  1.0f, 0.0f, 1.0f,  1.0f ,  // top right
				1.0f, -1.0f, 0.0f, 1.0f, 0.0f,  // bottom right
				-1.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // top left 
				// second triangle
				 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, // bottom right
				-1.0f, -1.0f, 0.0f,0.0f, 0.0f,  // bottom left
				-1.0f,  1.0f, 0.0f ,0.0f,  1.0f  // top left
			};

			int w, h;
			glfwGetWindowSize(window, &w, &h);
			backgroundShader->setFloat("width", w);
			backgroundShader->setFloat("height", h);
			backgroundShader->setFloat("dim", config["background-dim"].get<Float>());
			background->use();

			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
			glEnableVertexAttribArray(1);

			backgroundShader->use();

			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		void MainWindow::viewportWindow() {

			if (windowFlags.showView) {
				ImGui::PushID("render view texture");
				if (windowFlags.viewportUpdateAvailable) {
					std::lock_guard<std::mutex> lock(viewportMutex);
					loadViewImpl();
					windowFlags.viewportUpdateAvailable = false;
				}
				WindowFlag flag = ImGuiWindowFlags_NoScrollbar;
				if (engine->getStatus() == RenderEngine::ERenderInteractive) {
					flag |= ImGuiWindowFlags_NoMove;
				}
				Window().name("render view").open(&windowFlags.showView).flag(flag).with(true, [=]() {
					ImGui::Text("Camera Mode");
					ImGui::SameLine();
					if (ImGui::RadioButton("Free", cameraMode == EFree)) {
						cameraMode = EFree;
					}
					ImGui::SameLine();
					if (ImGui::RadioButton("Perspective", cameraMode == EPerspective)) {
						cameraMode = EPerspective;
					}

					if (!viewport)return;
					Draw(*viewport);
					auto graph = engine->getGraph();
					if (!graph)return;
					auto cam = graph->activeCamera;
					if (!cam)return;
					ImGuiIO& io = ImGui::GetIO();
					if (engine->getStatus() != RenderEngine::ERenderInteractive) {
						return;
					}
					bool restart = false;
					if (visitor.hasAnyChanged()) {
						restart = true;
						visitor.resetChanges(); 
					}
					cam->preprocess();
					Vec3f newPos = cam->viewpoint, newDir = cam->direction;
					auto pos = io.MousePos;
					if (!lastViewportMouseDown) {
						if (io.MouseDown[1]) {
							auto windowPos = ImGui::GetWindowPos();
							auto size = ImGui::GetWindowSize();
							if (pos.x >= windowPos.x && pos.y >= windowPos.y
								&& pos.x < windowPos.x + size.x && pos.y < windowPos.y + size.y) {
								lastViewportMouseDown = Point2i(pos.x, pos.y);
								cameraDir = cam->direction;
								cameraPos = cam->viewpoint;
								distance = (cameraPos - center.value()).length();

							}
						}
					}
					else if (!io.MouseDown[1]) {
						lastViewportMouseDown = {};
					}
					else {

						if (cameraMode == EPerspective) {
							if (io.MouseWheel > 0) {
								distance /= 1.1; restart = true;
							}
							else if (io.MouseWheel < 0) {
								distance *= 1.1; restart = true;
							}
						}
						else {
							auto bound = engine->getScene()->getWorldBound();
							Point3f _;
							float marchDistance;
							bound.boundingSphere(&_, &marchDistance);
							Float march = marchDistance * 0.025;
							if (io.KeyShift) {
								march *= 0.1f;
							}
							if (io.MouseWheel > 0) {
								restart = true;
							}
							else if (io.MouseWheel < 0) {
								march *= -1; restart = true;
							}
							if (restart) {
								Vec3f m = cam->cameraToWorld(Vec4f(0, 0, 1, 1));
								m.normalize();
								newPos = cam->viewpoint + march * m;
								Log::log("{}\n", march);
							}
						}
						auto delta = io.MouseDelta;
						if (delta.x != 0 || delta.y != 0 || restart) {
							restart = true;
							auto last = lastViewportMouseDown.value();
							if (cameraMode == EPerspective) {
								auto offset = -Vec3f(pos.x - last.x, (pos.y - last.y), 0.0f) / 500;
								newDir = cameraDir + Vec3f(offset.x, offset.y, 0);
								auto dir = Vec3f(
									std::sin(newDir.x) * std::cos(newDir.y),
									std::sin(newDir.y),
									std::cos(newDir.x) * std::cos(newDir.y));
								newPos = -dir * distance + center.value();
							}
							else {
								auto offset = -Vec3f(pos.x - last.x, (pos.y - last.y), 0.0f) / 500;
								newDir = cameraDir + Vec3f(offset.x, offset.y, 0);
							}
						}
					}
					if (restart) {
						engine->requestAbortRender();
						cam->viewpoint = newPos;
						cam->direction = newDir;
						engine->commit();
						engine->startInteractiveRender([=](Arc<Core::Film> film) {
							loadView(film);
						}, false);
					}
				}).show();
				ImGui::PopID();
			}
		}

		void MainWindow::explorerWindow() {
			if (windowFlags.showExplorer) {
				Window().name("Explorer")
					.open(&windowFlags.showExplorer)
					.with(true, [=] {
					visitor.visitGraph();
				}).show();
			}
		}

		void MainWindow::attriubuteEditorWindow() {
			if (windowFlags.showAttributeEditor) {
				auto showSelected = [=]() {
					try {
						visitor.visitSelected();
					}
					catch (std::exception& e) {
						Log::log("Error when visiting selected: {}\n", e.what());
					}
				};

				Window().name("Attribute")
					.open(&windowFlags.showAttributeEditor)
					.with(true, [=]() {

					Tab().item(TabItem().name("Inspector").with(true, showSelected))
						.item(TabItem().name("Sampling").with(true, [=]() {visitor.visitIntegrator(); }))
						.item(TabItem().name("World").with(true, [=]() {visitor.visitWorld(); }))
						.item(TabItem().name("Camera").with(true, [=]() {visitor.visitCamera(); }))
						.item(TabItem().name("Film").with(true, [=]() {visitor.visitFilm(); })).show();
				}).show();
			}
		}
		void MainWindow::showErrorModal(const std::string& title, const std::string& error) {
			openModal(title, [=]() {
				Text().name(error).show();
				Button().name("Close").with(true, [=] {
					closeModal();
				}).show();
			});
		}
		void MainWindow::menuBar() {
			auto newGraph = [=]() {
				Log::log("Creating new scene graph\n");
				engine->newGraph();
			};
			auto importObj = [=]() {
				if (!engine->hasGraph()) {
					showErrorModal("Error", "Must create a scene graph first");
					return;
				}
				if (engine->isFilenameEmpty()) {
					showErrorModal("Error", "Save the scene graph first");
					return;
				}
				auto filename = IO::GetOpenFileNameWithDialog("Wavefront OBJ\0*.obj");
				if (filename.empty())return;
				std::thread th([=]() {
					modal.name("Importing"); modal.flag(ImGuiWindowFlags_AlwaysAutoResize);
					openModal([]() {});
					engine->importObj(filename);
					Log::log("Imported {}\n", filename);
					closeModal();
				});
				th.detach();
			};
			auto openFile = [=]() {
				auto filename = IO::GetOpenFileNameWithDialog("Scene description\0*.json\0Any File\0*.*");
				if (filename.empty())return;
				std::thread th([=]() {
					openModal("Opening scene description", []() {});
					engine->open(filename);
					closeModal();
				});
				th.detach();
			};
			auto saveFile = [=]() {
				if (!engine->hasGraph()) {
					showErrorModal("Error", "Must create a scene graph first");
					return;
				}
				if (engine->isFilenameEmpty()) {
					auto filename = IO::GetSaveFileNameWithDialog("Scene description\0*.json\0Any File\0*.*");
					if (!filename.empty())
						engine->firstSave(filename);
				}
				else {
					engine->save();
				}
			};
			auto saveAs = [=]() {
				if (!engine->hasGraph()) {
					showErrorModal("Error", "Must create a scene graph first");
					return;
				}
				auto filename = IO::GetSaveFileNameWithDialog("Scene description\0*.json\0Any File\0*.*");
				if (!filename.empty())
					engine->firstSave(filename);
			};
			auto closeFile = [=]() {
				newEngine();
			};
			auto startInteractive = [=]() {
				std::thread th([=]() {
					try {
						openModal("Starting rendering", []() {});
						engine->commit();
						auto scene = engine->getScene();
						auto bound = scene->getWorldBound();
						if (!this->center) {
							float r;
							Point3f center;
							bound.boundingSphere(&center, &r);
							this->center = Vec3f(center.x, center.y, center.z);
						}
						
						auto r = engine->startInteractiveRender([=](Arc<Core::Film> film) {
							loadView(film);
						}, true);
						if (!r) {
							showErrorModal("Error",
								"Cannot start rendering: no integrator or integrator does not support interactive\n");
						}else
						closeModal();
						
					}
					catch (std::exception& e) {
						Log::log("{}\n", e.what());
						closeModal();
					}
				});
				th.detach();
			};
			auto startProgressive = [=]() {
				visitor.startInteractive();
			};
			auto stopRender = [=]() {
				visitor.stopRender();
			};

			MainMenuBar()
				.menu(Menu().name("File")
					.item(MenuItem().name("Open").with(true, openFile))
					.item(MenuItem().name("New").with(true, newGraph))
					.item(MenuItem().name("Save").with(true, saveFile))
					.item(MenuItem().name("Save As").with(true, saveAs))
					.item(MenuItem().name("Close").with(true, closeFile))
					.item(MenuItem().name("Import").with(true, importObj))
					.item(MenuItem().name("Exit")))
				.menu(Menu().name("Window")
					.item(MenuItem().name("Explorer").selected(&windowFlags.showExplorer))
					.item(MenuItem().name("Attribute Editor").selected(&windowFlags.showAttributeEditor))
					.item(MenuItem().name("View").selected(&windowFlags.showView))
					.item(MenuItem().name("Log").selected(&windowFlags.showLog))
					.item(MenuItem().name("Preference").selected(&windowFlags.showPreference)))
				.menu(Menu().name("Render")
					.item(MenuItem().name("Start Interactive").with(true, startInteractive))
					.item(MenuItem().name("Start Progressive (Final)").with(true, startProgressive))
					.item(MenuItem().name("Stop").with(true, stopRender)))
				.menu(Menu().name("Help"))
				.show();
			if (windowFlags.showPreference) {
				Window().name("Preference").open(&windowFlags.showPreference).with(true, [=]() {
					/*if (ImGui::Button("Background Image")) {
						auto s = IO::GetOpenFileNameWithDialog();
						if (!s.empty()) {
							config["background-image"] = s;
							loadBackgroundImage();
						}
					}
					Float dim = 0;
					if (config.contains("background-dim"))
						dim = config["background-dim"].get<Float>();
					if (ImGui::SliderFloat("background dim", &dim, 0.0, 1.0)) {
						config["background-dim"] = dim;
					}*/
					Separator().show();
					ImGui::ShowStyleEditor();;
				}).show();
			}
		}

		void MainWindow::logWindow() {
			if (windowFlags.showLog) {
				Window().name("Log").open(&windowFlags.showLog).with(true, [=]() {
					Button().name("Clear").with(true, [=]() {
						LogWindowContent::GetInstance()->content.clear();
					}).show();
					Separator().show();
					// ???
				/*	ChildWindow().name("scolling")
						.flag(ImGuiWindowFlags_HorizontalScrollbar)
						.with(true, [=]() {
						LogWindowContent::GetInstance()->draw();
					}).show();*/
					if (ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
						LogWindowContent::GetInstance()->draw();
						ImGui::EndChild();
					}
				}).show();
			}
		}
		static void setUpDockSpace() {
			static bool opt_fullscreen_persistant = true;
			bool opt_fullscreen = opt_fullscreen_persistant;
			static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

			// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
			// because it would be confusing to have two docking targets within each others.
			ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
			if (opt_fullscreen)
			{
				ImGuiViewport* viewport = ImGui::GetMainViewport();
				ImGui::SetNextWindowPos(viewport->Pos);
				ImGui::SetNextWindowSize(viewport->Size);
				ImGui::SetNextWindowViewport(viewport->ID);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
				window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
				window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
			}

			// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
			if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
				window_flags |= ImGuiWindowFlags_NoBackground;

			// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
			// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive, 
			// all active windows docked into it will lose their parent and become undocked.
			// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise 
			// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin("DockSpace", nullptr, window_flags);
			ImGui::PopStyleVar();

			if (opt_fullscreen)
				ImGui::PopStyleVar(2);

			// DockSpace
			ImGuiIO& io = ImGui::GetIO();

			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
			

			ImGui::End();
		}
		void MainWindow::update() {
			setUpDockSpace();
			logWindow();
			try {
				showModal();
				menuBar();
				ImGui::ShowDemoWindow();
				viewportWindow();
				explorerWindow();
				attriubuteEditorWindow();
			}
			catch (std::exception& e) {
				Log::log("Unable to update ui dure to {}\n", e.what());
			}
		}

		void MainWindow::loadConfig() {
			std::ifstream in("config.json");
			if (!in) {
				config = {};
				config["background-dim"] = 0.0f;
			}
			else {
				std::string content((std::istreambuf_iterator<char>(in)),
					(std::istreambuf_iterator<char>()));
				config = json::parse(content);
			}
		}

		void MainWindow::saveConfig() {
			cxx::filesystem::current_path(programPath);
			std::ofstream out("config.json");
			out << config << std::endl;
		}

		void MainWindow::loadViewImpl() {
			auto graph = engine->getGraph();
			viewport = std::make_unique<HW::Texture>(graph->filmConfig.dimension[0],
				graph->filmConfig.dimension[1],
				&pixelData[0]);
		}
		void MainWindow::loadView(Arc<Core::Film> film) {
			static std::mutex mutex;
			std::unique_lock<std::mutex> lock(mutex);
			if (!lock.owns_lock())return;
			auto graph = engine->getGraph();
			size_t w = film->width(), h = film->height();
			if (w == graph->filmConfig.dimension[0] && h == graph->filmConfig.dimension[1]) {
				pixelData.resize(w * h * 4ull);
				/*size_t i = 0;
				for (const auto& pixel : film->image) {
					auto color = pixel.eval().toInt();
					pixelData[4ul * i] = color.r;
					pixelData[4ul * i + 1] = color.g;
					pixelData[4ul * i + 2] = color.b;
					pixelData[4ul * i + 3] = 255;
					i++;
				}*/
				Thread::ParallelFor(0, graph->filmConfig.dimension[1], [&](int j, int) {
					for (size_t i = 0; i < graph->filmConfig.dimension[0]; i++) {
						size_t offset = i + (size_t)j * graph->filmConfig.dimension[0];
						auto color = film->getPixel(Point2f(i, j)).eval().gamma();// .toInt();
						pixelData[4ull * offset] = color.r;
						pixelData[4ull * offset + 1] = color.g;
						pixelData[4ull * offset + 2] = color.b;
						pixelData[4ull * offset + 3] = 1.0f;// 255;
					}
				});
			}
			else {
				pixelData.resize(graph->filmConfig.dimension[0] * graph->filmConfig.dimension[1] * 4ull);
				Point2f scale = Point2f(w, h) / Point2f(graph->filmConfig.dimension[0],
					graph->filmConfig.dimension[1]);
				Thread::ParallelFor(0, graph->filmConfig.dimension[1], [&](int j, int) {
					for (size_t i = 0; i < graph->filmConfig.dimension[0]; i++) {
						size_t offset = i + (size_t)j * graph->filmConfig.dimension[0];
						auto color = film->getPixel(Point2f(i, j) * scale).eval().gamma();// .toInt();
						pixelData[4ull * offset] = color.r;
						pixelData[4ull * offset + 1] = color.g;
						pixelData[4ull * offset + 2] = color.b;
						pixelData[4ull * offset + 3] = 1.0f;//255;
					}
				});
			}
			viewportUpdate = film;
			{
				std::lock_guard<std::mutex> lock(viewportMutex);
				windowFlags.viewportUpdateAvailable = true;
			}
		}

		MainWindow::MainWindow(int argc, char** argv) :visitor(*this) {
			LogWindowContent::GetInstance();
			programPath = cxx::filesystem::current_path();
			glfwSetErrorCallback(glfw_error_callback);
			if (!glfwInit())
				std::exit(1);

			// Decide GL+GLSL versions
#if __APPLE__
			// GL 3.2 + GLSL 150
			const char* glsl_version = "#version 150";
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
			// GL 3.0 + GLSL 130
			const char* glsl_version = "#version 130";
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
			//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
			//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

			window = glfwCreateWindow(1920, 1080, "myk-gui", NULL, NULL);
			if (window == NULL)
				std::exit(1);
			glfwMakeContextCurrent(window);
			glfwSwapInterval(1); // Enable vsync

			// Initialize OpenGL loader
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
			bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
			bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
			bool err = gladLoadGL() == 0;
#else
			bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
			if (err)
			{
				fprintf(stderr, "Failed to initialize OpenGL loader!\n");
				std::exit(1);
			}
			// Setup Dear ImGui context
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO(); (void)io;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;;
			//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
			//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

			// Setup Dear ImGui style
			ImGui::StyleColorsDark();
			//ImGui::StyleColorsClassic();

			// Setup Platform/Renderer bindings
			ImGui_ImplGlfw_InitForOpenGL(window, true);
			ImGui_ImplOpenGL3_Init(glsl_version);

			//io.Fonts->AddFontDefault();
			io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/Consola.ttf", 16.0f);

			viewport = std::make_unique<HW::Texture>(1280, 720);
			loadConfig();
			//	loadBackgroundImage();

			//	loadBackGroundShader();

			newEngine();

			Log::log("Application started\n");

		}
		void MainWindow::mainLoop() {
			ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
			// Main loop
			while (!glfwWindowShouldClose(window)) {

				glfwPollEvents();

				// Start the Dear ImGui frame
				ImGui_ImplOpenGL3_NewFrame();
				ImGui_ImplGlfw_NewFrame();
				ImGui::NewFrame();

				update();

				// Rendering
				ImGui::Render();
				int display_w, display_h;
				glfwMakeContextCurrent(window);
				glfwGetFramebufferSize(window, &display_w, &display_h);
				glViewport(0, 0, display_w, display_h);
				glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
				glClear(GL_COLOR_BUFFER_BIT);
				//	drawBackground();
				ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

				glfwMakeContextCurrent(window);
				glfwSwapBuffers(window);

			}
			engine = nullptr;
		}
		void MainWindow::show() {
			mainLoop();
			close();
		}

	}
}