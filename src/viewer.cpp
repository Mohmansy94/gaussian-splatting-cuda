#include "config.h" // Include generated config
#include "visualizer/detail.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

#ifdef CUDA_GL_INTEROP_ENABLED
#include "visualizer/cuda_gl_interop.hpp"
#endif

namespace gs {

    ViewerDetail* ViewerDetail::detail_ = nullptr;

    ViewerDetail::ViewerDetail(std::string title, int width, int height)
        : title_(title),
          viewport_(width, height) {
        detail_ = this;
    }

    ViewerDetail::~ViewerDetail() {
        std::cout << "Viewer destroyed." << std::endl;
    }

    bool ViewerDetail::init() {

        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW!" << std::endl;
            return false;
        }

        glfwWindowHint(GLFW_SAMPLES, 8);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);

        window_ = glfwCreateWindow(
            detail_->viewport_.windowSize.x,
            detail_->viewport_.windowSize.y,
            detail_->title_.c_str(), NULL, NULL);

        if (window_ == NULL) {
            std::cerr << "Failed to create GLFW window!" << std::endl;
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(window_);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "GLAD init failed" << std::endl;
            glfwTerminate();
            return false;
        }

        glfwSwapInterval(1); // Enable vsync

        glfwSetMouseButtonCallback(window_, mouseButtonCallback);
        glfwSetCursorPosCallback(window_, cursorPosCallback);
        glfwSetScrollCallback(window_, scrollCallback);
        glfwSetKeyCallback(window_, wsad_callback);

        glEnable(GL_LINE_SMOOTH);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glEnable(GL_PROGRAM_POINT_SIZE);

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        ImGui::StyleColorsLight();

        // Setup Platform/Renderer backends
        const char* glsl_version = "#version 430";
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init(glsl_version);

        // Set Fonts
        std::string font_path = std::string(PROJECT_ROOT_PATH) +
                                "/include/visualizer/assets/JetBrainsMono-Regular.ttf";
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 14.0f);

        // Set Windows option
        window_flags |= ImGuiWindowFlags_NoScrollbar;
        window_flags |= ImGuiWindowFlags_NoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.WindowPadding = ImVec2(6.0f, 6.0f);
        style.WindowRounding = 6.0f;
        style.WindowBorderSize = 0.0f;

        return true;
    }

    void ViewerDetail::updateWindowSize() {
        int winW, winH, fbW, fbH;
        glfwGetWindowSize(window_, &winW, &winH);
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        viewport_.windowSize = glm::ivec2(winW, winH);
        viewport_.frameBufferSize = glm::ivec2(fbW, fbH);
        glViewport(0, 0, fbW, fbH);
    }

    float ViewerDetail::getGPUUsage() {
        size_t free_byte, total_byte;
        cudaDeviceSynchronize();
        cudaMemGetInfo(&free_byte, &total_byte);
        size_t used_byte = total_byte - free_byte;
        float gpuUsage = used_byte / (float)total_byte * 100;

        return gpuUsage;
    }

    void ViewerDetail::setFrameRate(const int fps) {
        targetFPS = fps;
        frameTime = 1000 / targetFPS;
    }

    void ViewerDetail::controlFrameRate() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
        if (duration < frameTime) {
            std::this_thread::sleep_for(std::chrono::milliseconds(frameTime - duration));
        }
        lastTime = std::chrono::high_resolution_clock::now();
    }

    void ViewerDetail::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        if (detail_->any_window_active)
            return;

        if ((button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) && action == GLFW_PRESS) {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            detail_->viewport_.camera.initScreenPos(glm::vec2(xpos, ypos));
        }
    }

    void ViewerDetail::cursorPosCallback(GLFWwindow* window, double x, double y) {
        if (detail_->any_window_active)
            return;

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            detail_->viewport_.camera.translate(glm::vec2(x, y));
        } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            detail_->viewport_.camera.rotate(glm::vec2(x, y));
        } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            detail_->viewport_.camera.rotate_around_center(glm::vec2(x, y));
        }
    }

    void ViewerDetail::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        if (detail_->any_window_active)
            return;

        float delta = static_cast<float>(yoffset);
        if (std::abs(delta) < 1.0e-2f)
            return;

        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            // 'R' key is currently pressed
            detail_->viewport_.camera.rotate_roll(delta);
        } else {
            detail_->viewport_.camera.zoom(delta);
        }
    }

    void ViewerDetail::wsad_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {

        const float ADVANCE_RATE = 1.0f;
        const float ADVANCE_RATE_FINE_TUNE = 0.3f;

        float advance_rate = (action == GLFW_PRESS) ? ADVANCE_RATE_FINE_TUNE : (action == GLFW_REPEAT) ? ADVANCE_RATE
                                                                                                       : 0.0f;
        if (advance_rate == 0) {
            return;
        }

        switch (key) {
        case GLFW_KEY_W:
            detail_->viewport_.camera.advance_forward(advance_rate);
            break;
        case GLFW_KEY_A:
            detail_->viewport_.camera.advance_left(advance_rate);
            break;
        case GLFW_KEY_S:
            detail_->viewport_.camera.advance_backward(advance_rate);
            break;
        case GLFW_KEY_D:
            detail_->viewport_.camera.advance_right(advance_rate);
            break;
        }
    }

    void ViewerDetail::run() {

        if (!init()) {
            std::cerr << "Viewer initialization failed!" << std::endl;
            return;
        }

        std::string shader_path = std::string(PROJECT_ROOT_PATH) + "/include/visualizer/shaders/";
        quadShader_ = std::make_shared<Shader>(
            (shader_path + "/screen_quad.vert").c_str(),
            (shader_path + "/screen_quad.frag").c_str(),
            true);

        // Initialize screen renderer with interop support if available
#ifdef CUDA_GL_INTEROP_ENABLED
        screen_renderer_ = std::make_shared<ScreenQuadRendererInterop>(true);
        std::cout << "CUDA-OpenGL interop enabled for rendering" << std::endl;
#else
        screen_renderer_ = std::make_shared<ScreenQuadRenderer>();
        std::cout << "Using CPU copy for rendering (interop not available)" << std::endl;
#endif

        while (!glfwWindowShouldClose(window_)) {

            // Clear with a dark background
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            controlFrameRate();

            updateWindowSize();

            draw();

            glfwSwapBuffers(window_);
            glfwPollEvents();
        }

        // Cleanup
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    GSViewer::GSViewer(std::string title, int width, int height)
        : ViewerDetail(title, width, height),
          trainer_(nullptr) {

        config_ = std::make_shared<RenderingConfig>();
        info_ = std::make_shared<TrainingInfo>();
        notifier_ = std::make_shared<Notifier>();
        scripting_console_ = std::make_unique<ScriptingConsole>();

        setFrameRate(30);

        // Set up default script executor with basic functionality
        setScriptExecutor([this](const std::string& command) -> std::string {
            std::ostringstream result;

            // Basic command parsing
            if (command.empty()) {
                return "";
            }

            // Handle basic commands
            if (command == "help" || command == "h") {
                result << "Available commands:\n";
                result << "  help, h - Show this help\n";
                result << "  clear - Clear console\n";
                result << "  status - Show training status\n";
                result << "  model_info - Show model information\n";
                result << "  tensor_info <name> - Show tensor information\n";
                result << "  gpu_info - Show GPU information\n";
                return result.str();
            }

            if (command == "clear") {
                scripting_console_->clearLog();
                return "";
            }

            if (command == "status") {
                if (!trainer_) {
                    return "No trainer available (viewer mode)";
                }
                result << "Training Status:\n";
                result << "  Running: " << (trainer_->is_running() ? "Yes" : "No") << "\n";
                result << "  Paused: " << (trainer_->is_paused() ? "Yes" : "No") << "\n";
                result << "  Complete: " << (trainer_->is_training_complete() ? "Yes" : "No") << "\n";
                result << "  Current Iteration: " << trainer_->get_current_iteration() << "\n";
                result << "  Current Loss: " << std::fixed << std::setprecision(6) << trainer_->get_current_loss();
                return result.str();
            }

            if (command == "model_info") {
                if (!trainer_ && !standalone_model_) {
                    return "No model available";
                }

                result << "Model Information:\n";

                if (trainer_) {
                    std::lock_guard<std::mutex> lock(splat_mtx_);
                    auto& model = trainer_->get_strategy().get_model();
                    result << "  Number of Gaussians: " << model.size() << "\n";
                    result << "  Positions shape: [" << model.get_means().size(0) << ", " << model.get_means().size(1) << "]\n";
                    result << "  Device: " << model.get_means().device() << "\n";
                    result << "  Dtype: " << model.get_means().dtype() << "\n";
                    result << "  Active SH degree: " << model.get_active_sh_degree() << "\n";
                    result << "  Scene scale: " << model.get_scene_scale();
                } else if (standalone_model_) {
                    std::lock_guard<std::mutex> lock(splat_mtx_);
                    result << "  Number of Gaussians: " << standalone_model_->size() << "\n";
                    result << "  Positions shape: [" << standalone_model_->get_means().size(0) << ", " << standalone_model_->get_means().size(1) << "]\n";
                    result << "  Device: " << standalone_model_->get_means().device() << "\n";
                    result << "  Dtype: " << standalone_model_->get_means().dtype() << "\n";
                    result << "  Active SH degree: " << standalone_model_->get_active_sh_degree() << "\n";
                    result << "  Scene scale: " << standalone_model_->get_scene_scale();
                    result << "\n  Mode: Viewer (no training)";
                }

                return result.str();
            }

            if (command == "gpu_info") {
                size_t free_byte, total_byte;
                cudaDeviceSynchronize();
                cudaMemGetInfo(&free_byte, &total_byte);

                double free_gb = free_byte / 1024.0 / 1024.0 / 1024.0;
                double total_gb = total_byte / 1024.0 / 1024.0 / 1024.0;
                double used_gb = total_gb - free_gb;

                result << "GPU Memory Info:\n";
                result << "  Total: " << std::fixed << std::setprecision(2) << total_gb << " GB\n";
                result << "  Used: " << used_gb << " GB\n";
                result << "  Free: " << free_gb << " GB\n";
                result << "  Usage: " << std::setprecision(1) << (used_gb / total_gb * 100.0) << "%";
                return result.str();
            }

            // Handle tensor_info command
            if (command.substr(0, 11) == "tensor_info") {
                if (!trainer_ && !standalone_model_) {
                    return "No model available";
                }

                std::string tensor_name = "";
                if (command.length() > 12) {
                    tensor_name = command.substr(12); // Get parameter after "tensor_info "
                }

                if (tensor_name.empty()) {
                    return "Usage: tensor_info <tensor_name>\nAvailable: means, scaling, rotation, shs, opacity";
                }

                std::lock_guard<std::mutex> lock(splat_mtx_);

                // Get model reference
                SplatData* model = nullptr;
                if (trainer_) {
                    model = const_cast<SplatData*>(&trainer_->get_strategy().get_model());
                } else if (standalone_model_) {
                    model = standalone_model_.get();
                }

                if (!model) {
                    return "Model not available";
                }

                torch::Tensor tensor;
                if (tensor_name == "means" || tensor_name == "positions") {
                    tensor = model->get_means();
                } else if (tensor_name == "scales" || tensor_name == "scaling") {
                    tensor = model->get_scaling();
                } else if (tensor_name == "rotations" || tensor_name == "rotation" || tensor_name == "quats") {
                    tensor = model->get_rotation();
                } else if (tensor_name == "features" || tensor_name == "colors" || tensor_name == "shs") {
                    tensor = model->get_shs();
                } else if (tensor_name == "opacities" || tensor_name == "opacity") {
                    tensor = model->get_opacity();
                } else {
                    return "Unknown tensor: " + tensor_name + "\nAvailable: means, scaling, rotation, shs, opacity";
                }

                result << "Tensor '" << tensor_name << "' info:\n";
                result << "  Shape: [";
                for (int i = 0; i < tensor.dim(); i++) {
                    if (i > 0)
                        result << ", ";
                    result << tensor.size(i);
                }
                result << "]\n";
                result << "  Device: " << tensor.device() << "\n";
                result << "  Dtype: " << tensor.dtype() << "\n";
                result << "  Requires grad: " << (tensor.requires_grad() ? "Yes" : "No") << "\n";

                // Show some statistics if tensor is on CPU or we can move it
                try {
                    auto cpu_tensor = tensor.cpu();
                    auto flat = cpu_tensor.flatten();
                    if (flat.numel() > 0) {
                        result << "  Min: " << torch::min(flat).item<float>() << "\n";
                        result << "  Max: " << torch::max(flat).item<float>() << "\n";
                        result << "  Mean: " << torch::mean(flat).item<float>() << "\n";
                        result << "  Std: " << torch::std(flat).item<float>();
                    }
                } catch (...) {
                    result << "  (Statistics unavailable)";
                }

                return result.str();
            }

            return "Unknown command: '" + command + "'. Type 'help' for available commands.";
        });
    }

    GSViewer::~GSViewer() {
        // If trainer is still running, request it to stop
        if (trainer_ && trainer_->is_running()) {
            std::cout << "Viewer closing - stopping training..." << std::endl;
            trainer_->request_stop();

            // Give the training thread a moment to acknowledge the stop request
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "GSViewer destroyed." << std::endl;
    }

    void GSViewer::setTrainer(Trainer* trainer) {
        trainer_ = trainer;
    }

    void GSViewer::setStandaloneModel(std::unique_ptr<SplatData> model) {
        standalone_model_ = std::move(model);
    }

    void GSViewer::setAntiAliasing(bool enable) {
        anti_aliasing_ = enable;
    }

    void GSViewer::setScriptExecutor(std::function<std::string(const std::string&)> executor) {
        if (scripting_console_) {
            scripting_console_->execute_callback_ = executor;
        }
    }

    void GSViewer::renderScriptingConsole() {
        if (!show_scripting_console_ || !scripting_console_) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.15f, 0.25f, 1.0f));

        if (!ImGui::Begin("Scripting Console", &show_scripting_console_, ImGuiWindowFlags_MenuBar)) {
            ImGui::End();
            ImGui::PopStyleColor(4);
            return;
        }

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Console")) {
                if (ImGui::MenuItem("Clear", "Ctrl+L")) {
                    scripting_console_->clearLog();
                }
                if (ImGui::MenuItem("Copy Output")) {
                    std::string output;
                    for (const auto& line : scripting_console_->output_buffer_) {
                        output += line + "\n";
                    }
                    ImGui::SetClipboardText(output.c_str());
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Output area
        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

            for (const auto& line : scripting_console_->output_buffer_) {
                ImVec4 color;
                bool has_color = false;

                // Color coding for different types of output
                if (line.find(">>>") == 0) {
                    color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); // Yellow for commands
                    has_color = true;
                } else if (line.find("Error:") == 0) {
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red for errors
                    has_color = true;
                } else if (line.find("Info:") == 0 || line.find("GPU Memory") == 0 || line.find("Model Information") == 0 || line.find("Training Status") == 0) {
                    color = ImVec4(0.3f, 0.8f, 0.3f, 1.0f); // Green for info
                    has_color = true;
                }

                if (has_color)
                    ImGui::PushStyleColor(ImGuiCol_Text, color);

                ImGui::TextUnformatted(line.c_str());

                if (has_color)
                    ImGui::PopStyleColor();
            }

            ImGui::PopStyleVar();

            if (scripting_console_->scroll_to_bottom_ || ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            scripting_console_->scroll_to_bottom_ = false;
        }
        ImGui::EndChild();

        // Command input
        ImGui::Separator();

        // Input field - fix colors for visibility
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));         // Dark background
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.25f, 0.25f, 0.3f, 1.0f)); // Slightly lighter on hover
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));   // Even lighter when active
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));             // White text

        bool reclaim_focus = false;
        ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                               ImGuiInputTextFlags_CallbackCompletion |
                                               ImGuiInputTextFlags_CallbackHistory;

        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##input", scripting_console_->input_buffer_, sizeof(scripting_console_->input_buffer_),
                             input_text_flags, &ScriptingConsole::textEditCallbackStub, (void*)scripting_console_.get())) {

            std::string command = scripting_console_->input_buffer_;
            if (!command.empty()) {
                scripting_console_->executeCommand(command);
                scripting_console_->input_buffer_[0] = 0;
                reclaim_focus = true;
            }
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(4); // Pop the 4 colors we pushed

        // Auto-focus on window appearing
        ImGui::SetItemDefaultFocus();
        if (reclaim_focus)
            ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

        ImGui::End();
        ImGui::PopStyleColor(4);
    }

    void GSViewer::drawFrame() {
        // Only render if we have a model to render
        if (!trainer_ && !standalone_model_) {
            return;
        }

        glm::mat3 R = viewport_.getRotationMatrix();
        glm::vec3 t = viewport_.getTranslation();

        torch::Tensor R_tensor = torch::tensor({R[0][0], R[1][0], R[2][0],
                                                R[0][1], R[1][1], R[2][1],
                                                R[0][2], R[1][2], R[2][2]},
                                               torch::TensorOptions().dtype(torch::kFloat32))
                                     .reshape({3, 3});

        torch::Tensor t_tensor = torch::tensor({t[0],
                                                t[1],
                                                t[2]},
                                               torch::TensorOptions().dtype(torch::kFloat32))
                                     .reshape({3, 1});

        R_tensor = R_tensor.transpose(0, 1);
        t_tensor = -R_tensor.mm(t_tensor).squeeze();

        glm::ivec2& reso = viewport_.windowSize;
        glm::vec2 fov = config_->getFov(reso.x, reso.y);

        Camera cam = Camera(
            R_tensor,
            t_tensor,
            fov2focal(fov.x, reso.x),
            fov2focal(fov.y, reso.y),
            reso.x / 2.0f,
            reso.y / 2.0f,
            torch::empty({0}, torch::kFloat32),
            torch::empty({0}, torch::kFloat32),
            gsplat::CameraModelType::PINHOLE,
            "online",
            "none image",
            reso.x,
            reso.y,
            -1);

        torch::Tensor background = torch::zeros({3});

        RenderOutput output;
        {
            std::lock_guard<std::mutex> lock(splat_mtx_);

            // Get model from trainer or standalone
            SplatData* model = nullptr;
            if (trainer_) {
                model = const_cast<SplatData*>(&trainer_->get_strategy().get_model());
            } else if (standalone_model_) {
                model = standalone_model_.get();
            }

            if (!model) {
                return;
            }

            output = gs::rasterize(
                cam,
                *model,
                background,
                config_->scaling_modifier,
                false,
                anti_aliasing_,
                RenderMode::RGB);
        }

#ifdef CUDA_GL_INTEROP_ENABLED
        // Use interop for direct GPU transfer
        auto interop_renderer = std::dynamic_pointer_cast<ScreenQuadRendererInterop>(screen_renderer_);

        if (interop_renderer && interop_renderer->isInteropEnabled()) {
            // Keep data on GPU - convert [C, H, W] to [H, W, C] format
            auto image_hwc = output.image.permute({1, 2, 0}).contiguous();

            // Direct CUDA->OpenGL update (no CPU copy!)
            interop_renderer->uploadFromCUDA(image_hwc, reso.x, reso.y);
        } else {
            // Fallback to CPU copy
            auto image = (output.image * 255).to(torch::kCPU).to(torch::kU8).permute({1, 2, 0}).contiguous();
            screen_renderer_->uploadData(image.data_ptr<uchar>(), reso.x, reso.y);
        }
#else
        // Original CPU copy path
        auto image = (output.image * 255).to(torch::kCPU).to(torch::kU8).permute({1, 2, 0}).contiguous();
        screen_renderer_->uploadData(image.data_ptr<uchar>(), reso.x, reso.y);
#endif

        screen_renderer_->render(quadShader_, viewport_);
    }

    void GSViewer::renderCameraControlsWindow() {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

        if (ImGui::Begin("Camera Controls", &show_camera_controls_window_, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Camera Controls:");
            ImGui::Separator();

            // Table for better formatting
            if (ImGui::BeginTable("camera_controls_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 400.0f);
                ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Local Translate Camera");
                ImGui::TableNextColumn();
                ImGui::Text("Left Mouse + Drag");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Local Rotate Camera (Pitch/Yaw)");
                ImGui::TableNextColumn();
                ImGui::Text("Right Mouse + Drag");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Rotate Around Scene Center");
                ImGui::TableNextColumn();
                ImGui::Text("Middle Mouse + Drag");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Zoom");
                ImGui::TableNextColumn();
                ImGui::Text("Mouse Scroll");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Roll Camera");
                ImGui::TableNextColumn();
                ImGui::Text("R + Mouse Scroll");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Move forward, backward, left and right within the scene");
                ImGui::TableNextColumn();
                ImGui::Text("w, s, a, d keys");

                ImGui::EndTable();
            }

            ImGui::Separator();
        }
        ImGui::End();
        ImGui::PopStyleColor(4);
    }

    void GSViewer::configuration() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        any_window_active = ImGui::IsAnyItemActive();

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
        ImGui::Begin("Rendering Setting", nullptr, window_flags);
        ImGui::SetWindowSize(ImVec2(300, 0));

        // Check if trainer or standalone model is set
        if (!trainer_ && !standalone_model_) {
            ImGui::Text("No model loaded.");
            ImGui::End();
            ImGui::PopStyleColor();
            return;
        }

        // Training control section - only show if trainer exists
        if (trainer_) {
            ImGui::Separator();
            ImGui::Text("Training Control");
            ImGui::Separator();

            bool is_training = trainer_->is_running();
            bool is_paused = trainer_->is_paused();
            bool is_complete = trainer_->is_training_complete();
            bool has_stopped = trainer_->has_stopped();

            // Show appropriate controls based on state
            if (!training_started_ && !is_training) {
                // Initial state - show start button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                if (ImGui::Button("Start Training", ImVec2(-1, 0))) {
                    manual_start_triggered_ = true;
                    training_started_ = true;
                }
                ImGui::PopStyleColor(2);
            } else if (is_complete || has_stopped) {
                // Training finished - show status
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
                                   has_stopped ? "Training Stopped!" : "Training Complete!");
            } else {
                // Training in progress - show control buttons

                // Pause/Resume button
                if (is_paused) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                    if (ImGui::Button("Resume", ImVec2(-1, 0))) {
                        trainer_->request_resume();
                    }
                    ImGui::PopStyleColor(2);

                    // When paused, show stop button too
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                    if (ImGui::Button("Stop Permanently", ImVec2(-1, 0))) {
                        trainer_->request_stop();
                    }
                    ImGui::PopStyleColor(2);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.6f, 0.2f, 1.0f));
                    if (ImGui::Button("Pause", ImVec2(-1, 0))) {
                        trainer_->request_pause();
                    }
                    ImGui::PopStyleColor(2);
                }

                // Save checkpoint button (always visible during training)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.7f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
                if (ImGui::Button("Save Checkpoint", ImVec2(-1, 0))) {
                    trainer_->request_save();
                    save_in_progress_ = true;
                    save_start_time_ = std::chrono::steady_clock::now();
                }
                ImGui::PopStyleColor(2);
            }

            // Show save progress feedback
            if (save_in_progress_) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - save_start_time_).count();
                if (elapsed < 2000) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Checkpoint saved!");
                } else {
                    save_in_progress_ = false;
                }
            }

            // Status display
            ImGui::Separator();
            int current_iter = trainer_->get_current_iteration();
            float current_loss = trainer_->get_current_loss();
            ImGui::Text("Status: %s", is_complete ? "Complete" : (is_paused ? "Paused" : (is_training ? "Training" : "Ready")));
            ImGui::Text("Iteration: %d", current_iter);
            ImGui::Text("Loss: %.6f", current_loss);

            // Display render mode
#ifdef CUDA_GL_INTEROP_ENABLED
            ImGui::Text("Render Mode: GPU Direct (Interop)");
#else
            ImGui::Text("Render Mode: CPU Copy");
#endif

            // Handle the start trigger
            if (notifier_ && manual_start_triggered_) {
                std::lock_guard<std::mutex> lock(notifier_->mtx);
                notifier_->ready = true;
                notifier_->cv.notify_one();
                manual_start_triggered_ = false;
            }
        } else {
            // Standalone model info (viewer mode)
            ImGui::Separator();
            ImGui::Text("Model Information");
            ImGui::Separator();

            if (standalone_model_) {
                ImGui::Text("Gaussians: %lld", standalone_model_->size());
                ImGui::Text("SH Degree: %d", standalone_model_->get_active_sh_degree());
                ImGui::Text("Scene Scale: %.3f", standalone_model_->get_scene_scale());
            }

            // Greyed out start training button
            ImGui::BeginDisabled(true);
            ImGui::Button("Start Training", ImVec2(-1, 0));
            ImGui::EndDisabled();

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Viewer mode - no training available");
        }

        ImGui::Separator();
        ImGui::Text("Rendering Settings");
        ImGui::Separator();

        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("##scale_slider", &config_->scaling_modifier, 0.01f, 3.0f, "Scale=%.2f");
        ImGui::SameLine();
        if (ImGui::Button("Reset##scale", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
            config_->scaling_modifier = 1.0f;
        }

        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("##fov_slider", &config_->fov, 45.0f, 120.0f, "FoV=%.2f");
        ImGui::SameLine();
        if (ImGui::Button("Reset##fov", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
            config_->fov = 75.0f;
        }

        // Only show training progress if trainer exists
        if (trainer_) {
            int current_iter2;
            int total_iter;
            int num_splats;
            std::vector<float> loss_data;
            {
                std::lock_guard<std::mutex> lock(info_->mtx);
                current_iter2 = info_->curr_iterations_;
                total_iter = info_->total_iterations_;
                num_splats = info_->num_splats_;
                loss_data.assign(info_->loss_buffer_.begin(), info_->loss_buffer_.end());
            }

            float fraction = total_iter > 0 ? float(current_iter2) / float(total_iter) : 0.0f;
            char overlay_text[64];
            std::snprintf(overlay_text, sizeof(overlay_text), "%d / %d", current_iter2, total_iter);
            ImGui::ProgressBar(fraction, ImVec2(-1, 20), overlay_text);

            if (loss_data.size() > 0) {
                auto [min_it, max_it] = std::minmax_element(loss_data.begin(), loss_data.end());
                float min_val = *min_it, max_val = *max_it;

                if (min_val == max_val) {
                    min_val -= 1.0f;
                    max_val += 1.0f;
                } else {
                    float margin = (max_val - min_val) * 0.05f;
                    min_val -= margin;
                    max_val += margin;
                }

                char loss_label[64];
                std::snprintf(loss_label, sizeof(loss_label), "Loss: %.4f", loss_data.back());

                ImGui::PlotLines(
                    "##Loss",
                    loss_data.data(),
                    static_cast<int>(loss_data.size()),
                    0,
                    loss_label,
                    min_val,
                    max_val,
                    ImVec2(-1, 50));
            }

            ImGui::Text("num Splats: %d", num_splats);
        }

        float gpuUsage = getGPUUsage();
        char gpuText[64];
        std::snprintf(gpuText, sizeof(gpuText), "GPU Usage: %.1f%%", gpuUsage);
        ImGui::ProgressBar(gpuUsage / 100.0f, ImVec2(-1, 20), gpuText);

        // Show Camera Controls button
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button("Show Camera Controls", ImVec2(-1, 0))) {
            show_camera_controls_window_ = true;
        }
        ImGui::PopStyleColor(2);

        // Scripting Console button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
        if (ImGui::Button("Open Console", ImVec2(-1, 0))) {
            show_scripting_console_ = true;
            if (scripting_console_) {
                scripting_console_->addLog("Console opened. Type 'help' for available commands.");
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::End();
        ImGui::PopStyleColor();

        // Camera Controls window
        if (show_camera_controls_window_) {
            renderCameraControlsWindow();
        }

        // Scripting Console window
        renderScriptingConsole();
    }

    void GSViewer::draw() {
        // Render 3D scene if available
        drawFrame();

        // ImGui UI
        configuration();

        // Render all ImGui elements
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

} // namespace gs