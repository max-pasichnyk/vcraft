#include "imgui.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <ResourceManager.hpp>
#include <glm/ext.hpp>

#include "RenderSystem.hpp"
#include "Image.hpp"

#include "gui.hpp"
#include "Window.hpp"
#include "Input.hpp"
#include "Clock.hpp"

struct CameraUniform {
	glm::mat4 camera;
};

inline static glm::mat4 PerspectiveProjectionMatrix(float field_of_view, float aspect_ratio, float near_plane, float far_plane) {
	const float tanHalfFovy = glm::tan(field_of_view * 0.5f);

	const float a = 1.0f / (aspect_ratio * tanHalfFovy);
	const float b = -1.0f / tanHalfFovy;
	const float c = far_plane / (far_plane - near_plane);
	const float d = (near_plane * far_plane) / (near_plane - far_plane);

	return {
		a, 0, 0, 0,
		0, b, 0, 0,
		0, 0, c, 1,
		0, 0, d, 0
	};
}

struct CommandPool {
	vk::CommandPool _commandPool;

	inline static CommandPool create(uint32_t queue_index, vk::CommandPoolCreateFlags flags) {
		vk::CommandPoolCreateInfo createInfo;
		createInfo.flags = flags;
		createInfo.queueFamilyIndex = queue_index;
		return {RenderSystem::Get()->device().createCommandPool(createInfo, nullptr)};
	}

	void destroy() {
		RenderSystem::Get()->device().destroyCommandPool(_commandPool);
	}

	inline vk::CommandBuffer allocate(vk::CommandBufferLevel level) {
		vk::CommandBufferAllocateInfo allocateInfo {
			.commandPool = _commandPool,
			.level = level,
			.commandBufferCount = 1
		};

		vk::CommandBuffer commandBuffer;
		RenderSystem::Get()->device().allocateCommandBuffers(&allocateInfo, &commandBuffer);
		return commandBuffer;
	}

	void free(vk::CommandBuffer commandBuffer) {
		RenderSystem::Get()->device().freeCommandBuffers(_commandPool, 1, &commandBuffer);
	}
};

struct AABB {
	float min_x;
	float min_y;
	float min_z;

	float max_x;
	float max_y;
	float max_z;
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;

	Vertex(float x, float y, float z, float nx, float ny, float nz) : position{x, y, z}, normal{nx, ny, nz} {}
};

struct VertexBuilder {
	std::vector<int> indices;
	std::vector<Vertex> vertices;

	void addQuad(int a1, int b1, int c1, int a2, int b2, int c2) {
		int idx = vertices.size();

		indices.reserve(6);

		indices.push_back(idx + a1);
		indices.push_back(idx + b1);
		indices.push_back(idx + c1);

		indices.push_back(idx + a2);
		indices.push_back(idx + b2);
		indices.push_back(idx + c2);
	}

	void addCube(float ox, float oy, float oz, float sx, float sy, float sz) {
		const float x0 = ox;
		const float y0 = oy;
		const float z0 = oz;
		
		const float x1 = ox + sx;
		const float y1 = oy + sy;
		const float z1 = oz + sz;

		// south
		addQuad(0, 1, 2, 0, 2, 3);
		vertices.emplace_back(x0, y0, z0, 0, 0, -1);
		vertices.emplace_back(x0, y1, z0, 0, 0, -1);
		vertices.emplace_back(x1, y1, z0, 0, 0, -1);
		vertices.emplace_back(x1, y0, z0, 0, 0, -1);

		// east
		addQuad(0, 1, 2, 0, 2, 3);
		vertices.emplace_back(x1, y0, z0, 1, 0, 0);
		vertices.emplace_back(x1, y1, z0, 1, 0, 0);
		vertices.emplace_back(x1, y1, z1, 1, 0, 0);
		vertices.emplace_back(x1, y0, z1, 1, 0, 0);

		// north
		addQuad(0, 1, 2, 0, 2, 3);
		vertices.emplace_back(x1, y0, z1, 0, 0, 1);
		vertices.emplace_back(x1, y1, z1, 0, 0, 1);
		vertices.emplace_back(x0, y1, z1, 0, 0, 1);
		vertices.emplace_back(x0, y0, z1, 0, 0, 1);

		// west
		addQuad(0, 1, 2, 0, 2, 3);
		vertices.emplace_back(x0, y0, z1, -1, 0, 0);
		vertices.emplace_back(x0, y1, z1, -1, 0, 0);
		vertices.emplace_back(x0, y1, z0, -1, 0, 0);
		vertices.emplace_back(x0, y0, z0, -1, 0, 0);

		// up
		addQuad(0, 1, 2, 0, 2, 3);
		vertices.emplace_back(x0, y1, z0, 0, 1, 0);
		vertices.emplace_back(x0, y1, z1, 0, 1, 0);
		vertices.emplace_back(x1, y1, z1, 0, 1, 0);
		vertices.emplace_back(x1, y1, z0, 0, 1, 0);

		// down
		addQuad(0, 1, 2, 0, 2, 3);
		vertices.emplace_back(x0, y0, z1, 0, -1, 0);
		vertices.emplace_back(x0, y0, z0, 0, -1, 0);
		vertices.emplace_back(x1, y0, z0, 0, -1, 0);
		vertices.emplace_back(x1, y0, z1, 0, -1, 0);
	}
};

struct Cube {
	glm::vec3 origin;
	glm::vec3 size;
	glm::vec3 uv;
};

struct Bone {
	std::string name;
	glm::vec3 pivot;
	std::vector<AABB> cubes;
};

#include "Json.hpp"

constexpr std::string_view geometry_humanoid = R"({
	"visible_bounds_width": 1,
	"visible_bounds_height": 1,
	"visible_bounds_offset": [ 0, 0.5, 0 ],
	"bones": [
		{
			"name": "body",
			"pivot": [ 0.0, 24.0, 0.0 ],
			"cubes": [
				{
					"origin": [ -4.0, 12.0, -2.0 ],
					"size": [ 8, 12, 4 ],
					"uv": [ 16, 16 ]
				}
			]
		},

		{
			"name": "waist",
			"neverRender": true,
			"pivot": [ 0.0, 12.0, 0.0 ]
		},

		{
			"name": "head",
			"pivot": [ 0.0, 24.0, 0.0 ],
			"cubes": [
				{
					"origin": [ -4.0, 24.0, -4.0 ],
					"size": [ 8, 8, 8 ],
					"uv": [ 0, 0 ]
				}
			]
		},

		{
			"name": "hat",
			"pivot": [ 0.0, 24.0, 0.0 ],
			"cubes": [
				{
					"origin": [ -4.0, 24.0, -4.0 ],
					"size": [ 8, 8, 8 ],
					"uv": [ 32, 0 ],
					"inflate": 0.5
				}
			],
			"neverRender": true
		},

		{
			"name": "rightArm",
			"pivot": [ -5.0, 22.0, 0.0 ],
			"cubes": [
				{
					"origin": [ -8.0, 12.0, -2.0 ],
					"size": [ 4, 12, 4 ],
					"uv": [ 40, 16 ]
				}
			]
		},

		{
			"name": "rightItem",
			"pivot": [ -6, 15, 1 ],
			"neverRender": true,
			"parent": "rightArm"
		},

		{
			"name": "leftArm",
			"pivot": [ 5.0, 22.0, 0.0 ],
			"cubes": [
				{
					"origin": [ 4.0, 12.0, -2.0 ],
					"size": [ 4, 12, 4 ],
					"uv": [ 40, 16 ]
				}
			],
			"mirror": true
		},

		{
			"name": "rightLeg",
			"pivot": [ -1.9, 12.0, 0.0 ],
			"cubes": [
				{
					"origin": [ -3.9, 0.0, -2.0 ],
					"size": [ 4, 12, 4 ],
					"uv": [ 0, 16 ]
				}
			]
		},

		{
			"name": "leftLeg",
			"pivot": [ 1.9, 12.0, 0.0 ],
			"cubes": [
				{
					"origin": [ -0.1, 0.0, -2.0 ],
					"size": [ 4, 12, 4 ],
					"uv": [ 0, 16 ]
				}
			],
			"mirror": true
		},

		{
			"name": "helmet",
			"pivot": [ 0.0, 24.0, 0.0 ],
			"neverRender": true
		},
		{
			"name": "rightArmArmor",
			"pivot": [ -5.0, 22.0, 0.0 ],
			"parent": "rightArm"
		},
		{
			"name": "leftArmArmor",
			"pivot": [ 5.0, 22.0, 0.0 ],
			"parent": "leftArm",
			"mirror": true
		},
		{
			"name": "rightLegging",
			"pivot": [ -1.9, 12.0, 0.0 ],
			"parent": "rightLeg"
		},
		{
			"name": "leftLegging",
			"pivot": [ 1.9, 12.0, 0.0 ],
			"parent": "leftLeg",
			"mirror": true
		},
		{
			"name": "rightBoot",
			"pivot": [ -1.9, 12.0, 0.0 ],
			"parent": "rightLeg"
		},
		{
			"name": "leftBoot",
			"pivot": [ 1.9, 12.0, 0.0 ],
			"parent": "leftLeg",
			"mirror": true
		},
		{
			"name": "rightSock",
			"pivot": [ -1.9, 12.0, 0.0 ],
			"parent": "rightLeg"
		},
		{
			"name": "leftSock",
			"pivot": [ 1.9, 12.0, 0.0 ],
			"parent": "leftLeg",
			"mirror": true
		},
		{
			"name": "bodyArmor",
			"pivot": [ 0.0, 24.0, 0.0 ],
			"parent": "body"
		},
		{
			"name": "belt",
			"pivot": [ 0.0, 24.0, 0.0 ],
			"parent": "body"
		}
	]
})";

struct Renderer {
	RenderSystem* core = RenderSystem::Get();

	int _width;
	int _height;

	vk::PipelineLayout _pipelineLayout;
	vk::Pipeline _pipeline;
//	vk::Sampler _sampler;

	Input* _input;
	RenderBuffer _buffer;
	VertexBuilder _builder;

	glm::mat4 _projection;
	glm::vec3 _cameraPosition{0, 1.5f, -2};

	void initialize(Window& window, Input& input, ResourceManager& rm, vk::DescriptorPool descriptorPool, vk::RenderPass renderPass) {
		_input = &input;

		_width = window.width();
		_height = window.height();

		_projection = PerspectiveProjectionMatrix(
				glm::radians(60.0f),
				float(_width) / float(_height),
				0.1f,
				1000.0f
		);

		vk::ShaderModule vertShader{nullptr};
		rm.loadFile("default.vert.spv", [&](std::span<char> bytes) {
			vertShader = core->device().createShaderModule({
					.codeSize = bytes.size(),
					.pCode = reinterpret_cast<const uint32_t *>(bytes.data())
			});
		});

		vk::ShaderModule fragShader{nullptr};
		rm.loadFile("default.frag.spv", [&](std::span<char> bytes) {
			fragShader = core->device().createShaderModule({
					.codeSize = bytes.size(),
					.pCode = reinterpret_cast<const uint32_t *>(bytes.data())
			});
		});

//			vk::SamplerCreateInfo sampler_create_info{
//					.magFilter = vk::Filter::eLinear,
//					.minFilter = vk::Filter::eLinear,
//					.mipmapMode = vk::SamplerMipmapMode::eLinear,
//					.addressModeU = vk::SamplerAddressMode::eRepeat,
//					.addressModeV = vk::SamplerAddressMode::eRepeat,
//					.addressModeW = vk::SamplerAddressMode::eRepeat,
//					.maxAnisotropy = 1.0f,
//					.minLod = -1000,
//					.maxLod = 1000
//			};

//			_sampler = core->device().createSampler(sampler_create_info, nullptr);

//			vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding{
//					.binding = 0,
//					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
//					.descriptorCount = 1,
//					.stageFlags = vk::ShaderStageFlagBits::eFragment,
//					.pImmutableSamplers = &_sampler
//			};

//			vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
//					.bindingCount = 1,
//					.pBindings = &descriptorSetLayoutBinding
//			};
//			_descriptorSetLayout = core->device().createDescriptorSetLayout(descriptor_set_layout_create_info, nullptr);
//			_fontDescriptor = vkx::allocate(_descriptorPool, _descriptorSetLayout);

		vk::PushConstantRange constant {
			vk::ShaderStageFlagBits::eVertex, 0, sizeof(CameraUniform)
		};

		vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{
				.setLayoutCount = /*1*/0,
				.pSetLayouts = nullptr,
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &constant
		};
		_pipelineLayout = core->device().createPipelineLayout(pipelineLayoutCreateInfo, nullptr);

		vk::PipelineShaderStageCreateInfo stages[] {
				{.stage = vk::ShaderStageFlagBits::eVertex, .module = vertShader, .pName = "main"},
				{.stage = vk::ShaderStageFlagBits::eFragment, .module = fragShader, .pName = "main"},
		};

		vk::VertexInputBindingDescription bindings[] {
				{0, sizeof(Vertex), vk::VertexInputRate::eVertex}
		};

		vk::VertexInputAttributeDescription attributes[]{
				{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)},
				{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)},
		};

		vk::PipelineVertexInputStateCreateInfo vertexInputState{
				.vertexBindingDescriptionCount = std::size(bindings),
				.pVertexBindingDescriptions = bindings,
				.vertexAttributeDescriptionCount = std::size(attributes),
				.pVertexAttributeDescriptions = attributes
		};

		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
				.topology = vk::PrimitiveTopology::eTriangleList,
				.primitiveRestartEnable = false
		};

		vk::Viewport viewport{
			.x = 0,
			.y = 0,
			.width = float(_width),
			.height = float(_height),
			.minDepth = 0,
			.maxDepth = 1
		};


		vk::Rect2D scissor {
			{0, 0}, {uint32_t(_width), uint32_t(_height)}
		};

		vk::PipelineViewportStateCreateInfo viewportState{
				.viewportCount = 1,
				.pViewports = &viewport,
				.scissorCount = 1,
				.pScissors = &scissor
		};

		vk::PipelineRasterizationStateCreateInfo rasterizationState{
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eNone,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.lineWidth = 1.0f
		};

		vk::PipelineMultisampleStateCreateInfo multisampleState;

		vk::PipelineColorBlendAttachmentState colorBlendAttachmentState{
				.blendEnable = VK_TRUE,
				.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
				.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
				.colorBlendOp = vk::BlendOp::eAdd,
				.srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
				.dstAlphaBlendFactor = vk::BlendFactor::eZero,
				.alphaBlendOp = vk::BlendOp::eAdd,
				.colorWriteMask = vk::ColorComponentFlagBits::eR |
						vk::ColorComponentFlagBits::eG |
						vk::ColorComponentFlagBits::eB |
						vk::ColorComponentFlagBits::eA
		};

		vk::PipelineDepthStencilStateCreateInfo depthStencilState {
			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = vk::CompareOp::eLess
		};

		vk::PipelineColorBlendStateCreateInfo colorBlendState{
				.attachmentCount = 1,
				.pAttachments = &colorBlendAttachmentState
		};

//		vk::DynamicState dynamic_states[]{
//				vk::DynamicState::eViewport,
//				vk::DynamicState::eScissor
//		};
//
//		vk::PipelineDynamicStateCreateInfo dynamicState{
//				.dynamicStateCount = 2,
//				.pDynamicStates = dynamic_states
//		};

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo{
				.stageCount = 2,
				.pStages = stages,
				.pVertexInputState = &vertexInputState,
				.pInputAssemblyState = &inputAssemblyState,
				.pViewportState = &viewportState,
				.pRasterizationState = &rasterizationState,
				.pMultisampleState = &multisampleState,
				.pDepthStencilState = &depthStencilState,
				.pColorBlendState = &colorBlendState,
//				.pDynamicState = &dynamicState,
				.layout = _pipelineLayout,
				.renderPass = renderPass,
		};

		core->device().createGraphicsPipelines(nullptr, 1, &pipelineCreateInfo, nullptr, &_pipeline);
		core->device().destroyShaderModule(vertShader);
		core->device().destroyShaderModule(fragShader);

		json::Parser parser(geometry_humanoid);

		auto model = std::get<json::Object>(parser.parse().value());
		auto&& bones = std::get<json::Array>(model.at("bones"));

		for (auto&& bone_obj : bones) {
			auto&& bone = std::get<json::Object>(bone_obj);

			bool neverRender = false;
			if (bone.contains("neverRender")) {
				neverRender = std::get<bool>(bone.at("neverRender"));
			}

			if (bone.contains("cubes")) {
				auto &&cubes = std::get<json::Array>(bone.at("cubes"));
				for (auto &&cube_obj : cubes) {
					auto &&cube = std::get<json::Object>(cube_obj);
					auto &&origin = std::get<json::Array>(cube.at("origin"));
					auto &&size = std::get<json::Array>(cube.at("size"));
					auto &&uv = std::get<json::Array>(cube.at("uv"));

					if (!neverRender) {
						_builder.addCube(
							std::get<double>(origin[0]) / 16.0f,
							std::get<double>(origin[1]) / 16.0f,
							std::get<double>(origin[2]) / 16.0f,
							std::get<int64_t>(size[0]) / 16.0f,
							std::get<int64_t>(size[1]) / 16.0f,
							std::get<int64_t>(size[2]) / 16.0f
						);
					}
				}
			}
		}

//		_builder.addCube(-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f);

		_buffer.SetIndexBufferSize(sizeof(int) * _builder.indices.size());
		_buffer.SetVertexBufferSize(sizeof(Vertex) * _builder.vertices.size());

		_buffer.SetIndexBufferData(_builder.indices.data(), 0, 0, sizeof(int) * _builder.indices.size());
		_buffer.SetVertexBufferData(_builder.vertices.data(), 0, 0, sizeof(Vertex) * _builder.vertices.size());
	}

	float rotationYaw{0};
	float rotationPitch{0};

	glm::mat4 rotationMatrix() {
		float yaw = glm::radians(rotationYaw);
		float pitch = glm::radians(rotationPitch);

		float sp = glm::sin(pitch);
		float cp = glm::cos(pitch);

		float c = glm::cos(yaw);
		float s = glm::sin(yaw);

		glm::mat4 view;
		view[0] = glm::vec4(c, sp * s, -cp * s, 0);
		view[1] = glm::vec4(0, cp, sp, 0);
		view[2] = glm::vec4(s, -sp * c, cp * c, 0);
		view[3] = glm::vec4(0, 0, 0, 1);
		return view;
	}

	void update(Clock& clock) {
		if (_input->IsMouseButtonPressed(MouseButton::Left)) {
			_input->setCursorState(CursorState::Locked);

			auto [xdelta, ydelta] = _input->mouseDelta();

			if (xdelta != 0 || ydelta != 0) {
				double d4 = 0.5f * (double) 0.6F + (double) 0.2F;
				double d5 = d4 * d4 * d4 * 8.0;

				rotationYaw = rotationYaw - xdelta * d5 * clock.deltaSeconds() * 9.0;
				rotationPitch = glm::clamp(rotationPitch - ydelta * d5 * clock.deltaSeconds() * 9.0, -90.0, 90.0);
			}
		} else {
			_input->setCursorState(CursorState::Normal);
		}

		auto rot = glm::mat3(rotationMatrix());
		auto forward = glm::vec3(0, 0, 1) * rot;
		auto right = glm::vec3(1, 0, 0) * rot;

		if (_input->IsKeyPressed(Key::W)) {
			_cameraPosition += forward * clock.deltaSeconds() * 5.0f;
		}
		if (_input->IsKeyPressed(Key::S)) {
			_cameraPosition -= forward * clock.deltaSeconds() * 5.0f;
		}
		if (_input->IsKeyPressed(Key::A)) {
			_cameraPosition -= right * clock.deltaSeconds() * 5.0f;
		}
		if (_input->IsKeyPressed(Key::D)) {
			_cameraPosition += right * clock.deltaSeconds() * 5.0f;
		}
	}

	void draw(vk::CommandBuffer cmd) {
		vk::DeviceSize offset{0};

		vk::Buffer vertexBuffers[] {
			_buffer.VertexBuffer
		};

		auto proj = _projection;
		auto view = rotationMatrix();

		CameraUniform uniform {
			.camera = proj * glm::translate(view, -_cameraPosition)
		};

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);
		cmd.pushConstants(_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(CameraUniform), &uniform);
		cmd.bindVertexBuffers(0, 1, vertexBuffers, &offset);
		cmd.bindIndexBuffer(_buffer.IndexBuffer, 0, vk::IndexType::eUint32);

		cmd.drawIndexed(_builder.indices.size(), 1, 0, 0, 0);
	}

	void terminate() {
		core->device().destroyPipelineLayout(_pipelineLayout);
		core->device().destroyPipeline(_pipeline);
		_buffer.destroy();
	}
};

struct Application {
	RenderSystem* core = RenderSystem::Get();

	Window window;
	Input input;
	Clock clock;

	ResourceManager resourceManager{"assets"};

	vk::DescriptorPool _descriptorPool;
	Renderer _renderer;

	Application() {
		window.create(1280, 720, "Vulkan");
		input.SetWindow(window.handle());

		glfwSetWindowUserPointer(window.handle(), this);
		glfwSetWindowSizeCallback(window.handle(), [](GLFWwindow *window, int width, int height) {
			static_cast<Application*>(glfwGetWindowUserPointer(window))->handleWindowResize(width, height);
		});
		glfwSetFramebufferSizeCallback(window.handle(), [](GLFWwindow *window, int width, int height) {
			static_cast<Application*>(glfwGetWindowUserPointer(window))->handleFramebufferResize(width, height);
		});
		glfwSetWindowIconifyCallback(window.handle(), [](GLFWwindow *window, int iconified) {
			static_cast<Application*>(glfwGetWindowUserPointer(window))->handleIconify(iconified);
		});
		glfwSetKeyCallback(window.handle(), [](GLFWwindow *window, int key, int scancode, int action, int mods) {
			static_cast<Application *>(glfwGetWindowUserPointer(window))->keyCallback(key, scancode, action, mods);
		});
		glfwSetMouseButtonCallback(window.handle(), [](GLFWwindow *window, int mouseButton, int action, int mods) {
			static_cast<Application *>(glfwGetWindowUserPointer(window))->mouseButtonCallback(mouseButton, action, mods);
		});
		glfwSetCursorPosCallback(window.handle(), [](GLFWwindow *window, double xpos, double ypos) {
			static_cast<Application *>(glfwGetWindowUserPointer(window))->cursorPosCallback(xpos, ypos);
		});
		glfwSetScrollCallback(window.handle(), [](GLFWwindow *window, double xoffset, double yoffset) {
			static_cast<Application *>(glfwGetWindowUserPointer(window))->scrollCallback(xoffset, yoffset);
		});
		glfwSetCharCallback(window.handle(), [](GLFWwindow* window, unsigned int c) {
			static_cast<Application *>(glfwGetWindowUserPointer(window))->charCallback(c);
		});

		core->initialize(window.handle());

		_depthFormat = core->getSupportedDepthFormat();

		createSwapchain();
		createRenderPass();
		createSyncObjects();
		createFrameObjects();

		vk::DescriptorPoolSize pool_sizes[] = {
				{vk::DescriptorType::eSampler, 1000},
				{vk::DescriptorType::eCombinedImageSampler, 1000},
				{vk::DescriptorType::eSampledImage, 1000},
				{vk::DescriptorType::eStorageImage, 1000},
				{vk::DescriptorType::eUniformTexelBuffer, 1000},
				{vk::DescriptorType::eStorageTexelBuffer, 1000},
				{vk::DescriptorType::eUniformBuffer, 1000},
				{vk::DescriptorType::eStorageBuffer, 1000},
				{vk::DescriptorType::eUniformBufferDynamic, 1000},
				{vk::DescriptorType::eStorageBufferDynamic, 1000},
				{vk::DescriptorType::eInputAttachment, 1000}
		};

		vk::DescriptorPoolCreateInfo descriptor_pool_create_info {
			.maxSets = 1000,
			.poolSizeCount = std::size(pool_sizes),
			.pPoolSizes = pool_sizes
		};
		_descriptorPool = core->device().createDescriptorPool(descriptor_pool_create_info, nullptr);

		_renderer.initialize(window, input, resourceManager, _descriptorPool, _renderPass);
		_gui.initialize(window.handle(), _renderPass, _frameCount);
	}

	~Application() {
		core->device().waitIdle();

		_renderer.terminate();
		_gui.terminate();

		for (uint32_t i = 0; i < _frameCount; i++) {
			core->device().destroyFramebuffer(_framebuffers[i], nullptr);
			core->device().destroyImageView(_swapchainImageViews[i], nullptr);
			core->device().destroyImageView(_depthImageViews[i]);
			_depthImages[i].destroy();

			_commandPools[i].free(_commandBuffers[i]);
			_commandPools[i].destroy();

			core->device().destroyFence(_fences[i], nullptr);
			core->device().destroySemaphore(_imageAcquiredSemaphore[i]);
			core->device().destroySemaphore(_renderCompleteSemaphore[i]);
		}

		core->device().destroyRenderPass(_renderPass, nullptr);
		core->device().destroySwapchainKHR(_swapchain, nullptr);
		core->terminate();

		window.destroy();
	}

	void run() {
		clock.reset();
		while (!glfwWindowShouldClose(window.handle())) {
			clock.update();

			input.update();
			glfwPollEvents();

			_renderer.update(clock);

			_gui.begin();
			_gui.end();

			auto cmd = begin();
			_renderer.draw(cmd);
			_gui.draw(cmd);
			end();

		}
	}

private:
	inline static int getImageCountFromPresentMode(vk::PresentModeKHR present_mode) {
		switch (present_mode) {
		case vk::PresentModeKHR::eImmediate:
			return 1;
		case vk::PresentModeKHR::eMailbox:
			return 3;
		case vk::PresentModeKHR::eFifo:
		case vk::PresentModeKHR::eFifoRelaxed:
			return 2;
//		case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:
//			break;
//		case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR:
//			break;
		default:
			return 1;
		}
	}

	inline static vk::Extent2D selectSurfaceExtent(vk::Extent2D extent, const vk::SurfaceCapabilitiesKHR &surface_capabilities) {
		if (surface_capabilities.currentExtent.width != UINT32_MAX) {
			return surface_capabilities.currentExtent;
		}

		auto minExtent = surface_capabilities.minImageExtent;
		auto maxExtent = surface_capabilities.maxImageExtent;

		return {
				std::clamp(extent.width, minExtent.width, maxExtent.width),
				std::clamp(extent.height, minExtent.height, maxExtent.height)
		};
	}

	inline static vk::SurfaceFormatKHR selectSurfaceFormat(span<vk::SurfaceFormatKHR> surface_formats, span<vk::Format> request_formats, vk::ColorSpaceKHR request_color_space) {
		if (surface_formats.size() == 1) {
			if (surface_formats[0].format == vk::Format::eUndefined) {
				vk::SurfaceFormatKHR ret;
				ret.format = request_formats.front();
				ret.colorSpace = request_color_space;
				return ret;
			}
			return surface_formats[0];
		}

		for (size_t i = 0; i < request_formats.size(); i++)
			for (size_t k = 0; k < surface_formats.size(); k++)
				if (surface_formats[k].format == request_formats[i] &&
						surface_formats[k].colorSpace == request_color_space)
					return surface_formats[k];

		return surface_formats.front();
	}

	inline static vk::PresentModeKHR selectPresentMode(span<vk::PresentModeKHR> present_modes, span<vk::PresentModeKHR> request_modes) {
		for (size_t i = 0; i < request_modes.size(); i++)
			for (size_t j = 0; j < present_modes.size(); j++)
				if (request_modes[i] == present_modes[j])
					return request_modes[i];
		return vk::PresentModeKHR::eFifo;
	}

	inline void createSwapchain() {
		auto capabilities = core->physicalDevice().getSurfaceCapabilitiesKHR(core->surface());
		auto surfaceFormats = core->physicalDevice().getSurfaceFormatsKHR(core->surface());
		auto presentModes = core->physicalDevice().getSurfacePresentModesKHR(core->surface());

		vk::Format request_formats[] {
				vk::Format::eB8G8R8A8Unorm,
				vk::Format::eR8G8B8A8Unorm,
				vk::Format::eB8G8R8Unorm,
				vk::Format::eR8G8B8Unorm
		};

		vk::PresentModeKHR request_modes [] {
			vk::PresentModeKHR::eFifo
		};

		_surface_extent = selectSurfaceExtent({0, 0}, capabilities);
		_surface_format = selectSurfaceFormat(surfaceFormats, request_formats, vk::ColorSpaceKHR::eSrgbNonlinear);
		_present_mode = selectPresentMode(presentModes, request_modes);
		int image_count = getImageCountFromPresentMode(_present_mode);

		int min_image_count = image_count;
		if (min_image_count < capabilities.minImageCount) {
			min_image_count = capabilities.minImageCount;
		} else if (capabilities.maxImageCount != 0 && min_image_count > capabilities.maxImageCount) {
			min_image_count = capabilities.maxImageCount;
		}

		vk::SwapchainCreateInfoKHR swapchainCreateInfo {
			.surface = core->surface(),
			.minImageCount = static_cast<uint32_t>(min_image_count),
			.imageFormat = _surface_format.format,
			.imageColorSpace = _surface_format.colorSpace,
			.imageExtent = _surface_extent,
			.imageArrayLayers = 1,
			.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
			.preTransform = capabilities.currentTransform,
			.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
			.presentMode = _present_mode,
			.clipped = true,
			.oldSwapchain = nullptr
		};

		uint32_t queue_family_indices[] = {
				core->graphicsFamily(),
				core->presentFamily()
		};

		if (core->graphicsFamily() != core->presentFamily()) {
			swapchainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent;
			swapchainCreateInfo.queueFamilyIndexCount = 2;
			swapchainCreateInfo.pQueueFamilyIndices = queue_family_indices;
		}

		_swapchain = core->device().createSwapchainKHR(swapchainCreateInfo, nullptr);
		_swapchainImages = core->device().getSwapchainImagesKHR(_swapchain);
		_frameCount = _swapchainImages.size();
	}

	void createRenderPass() {
		vk::AttachmentDescription attachments[]{
				vk::AttachmentDescription{
						{},
						_surface_format.format,
						vk::SampleCountFlagBits::e1,
						vk::AttachmentLoadOp::eClear,
						vk::AttachmentStoreOp::eStore,
						vk::AttachmentLoadOp::eDontCare,
						vk::AttachmentStoreOp::eDontCare,
						vk::ImageLayout::eUndefined,
						vk::ImageLayout::ePresentSrcKHR
				},
				vk::AttachmentDescription{
						{},
						_depthFormat,
						vk::SampleCountFlagBits::e1,
						vk::AttachmentLoadOp::eClear,
						vk::AttachmentStoreOp::eStore,
						vk::AttachmentLoadOp::eDontCare,
						vk::AttachmentStoreOp::eDontCare,
						vk::ImageLayout::eUndefined,
						vk::ImageLayout::eDepthStencilAttachmentOptimal
				}
		};

		vk::AttachmentReference color_attachment{
				0, vk::ImageLayout::eColorAttachmentOptimal
		};

		vk::AttachmentReference depth_attachment{
				1, vk::ImageLayout::eDepthStencilAttachmentOptimal
		};

		vk::SubpassDescription subpass{
				{},
				vk::PipelineBindPoint::eGraphics,
				0,
				nullptr,
				1,
				&color_attachment,
				nullptr,
				&depth_attachment,
				0,
				nullptr
		};

		vk::SubpassDependency dependency{
				VK_SUBPASS_EXTERNAL, 0,
				vk::PipelineStageFlagBits::eColorAttachmentOutput,
				vk::PipelineStageFlagBits::eColorAttachmentOutput,
				{},
				vk::AccessFlagBits::eColorAttachmentWrite
		};

		vk::RenderPassCreateInfo render_pass_create_info{
				.attachmentCount = 2,
				.pAttachments = attachments,
				.subpassCount = 1,
				.pSubpasses = &subpass,
				.dependencyCount = 1,
				.pDependencies = &dependency,
		};
		_renderPass = core->device().createRenderPass(render_pass_create_info, nullptr);
	}

	void createSyncObjects() {
		_fences.resize(_frameCount);
		_imageAcquiredSemaphore.resize(_frameCount);
		_renderCompleteSemaphore.resize(_frameCount);

		for (uint32_t i = 0; i < _frameCount; i++) {
			_fences[i] = core->device().createFence({.flags = vk::FenceCreateFlagBits::eSignaled});
			_imageAcquiredSemaphore[i] = core->device().createSemaphore({}, nullptr);
			_renderCompleteSemaphore[i] = core->device().createSemaphore({}, nullptr);
		}
	}

	void createFrameObjects() {
		_commandPools.resize(_frameCount);
		_commandBuffers.resize(_frameCount);
		_framebuffers.resize(_frameCount);
		_depthImages.resize(_frameCount);
		_depthImageViews.resize(_frameCount);
		_swapchainImageViews.resize(_frameCount);

		vk::ImageViewCreateInfo swapchainImageViewCreateInfo{
				.viewType = vk::ImageViewType::e2D,
				.format = _surface_format.format,
				.subresourceRange = {
						vk::ImageAspectFlagBits::eColor,
						0, 1, 0, 1
				}
		};
		vk::ImageCreateInfo depthImageCreateInfo{
				.imageType = vk::ImageType::e2D,
				.format = _depthFormat,
				.extent = vk::Extent3D{
						_surface_extent.width,
						_surface_extent.height,
						1
				},
				.mipLevels = 1,
				.arrayLayers = 1,
				.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
		};
		vk::ImageViewCreateInfo depthImageViewCreateInfo{
				.viewType = vk::ImageViewType::e2D,
				.format = _depthFormat,
				.subresourceRange = {
						vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1
				}
		};
		for (uint32_t i = 0; i < _frameCount; i++) {
			swapchainImageViewCreateInfo.image = _swapchainImages[i];
			_swapchainImageViews[i] = core->device().createImageView(swapchainImageViewCreateInfo, nullptr);

			_depthImages[i] = Image::create(depthImageCreateInfo, {.usage = VMA_MEMORY_USAGE_GPU_ONLY});

			depthImageViewCreateInfo.image = _depthImages[i];
			_depthImageViews[i] = core->device().createImageView(depthImageViewCreateInfo, nullptr);

			vk::ImageView attachments[]{
					_swapchainImageViews[i],
					_depthImageViews[i]
			};

			vk::FramebufferCreateInfo framebuffer_create_info{
					.renderPass = _renderPass,
					.attachmentCount = 2,
					.pAttachments = attachments,
					.width = _surface_extent.width,
					.height = _surface_extent.height,
					.layers = 1
			};

			_framebuffers[i] = core->device().createFramebuffer(framebuffer_create_info, nullptr);
			_commandPools[i] = CommandPool::create(core->graphicsFamily(), vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
			_commandBuffers[i] = _commandPools[i].allocate(vk::CommandBufferLevel::ePrimary);
		}
	}

	vk::CommandBuffer begin() {
		static constinit auto timeout = std::numeric_limits<uint64_t>::max();
		auto semaphore = _imageAcquiredSemaphore[_semaphoreIndex];

		core->device().acquireNextImageKHR(_swapchain, timeout, semaphore, nullptr, &_frameIndex);
		core->device().waitForFences(1, &_fences[_frameIndex], true, timeout);
		core->device().resetFences(1, &_fences[_frameIndex]);

		_commandBuffers[_frameIndex].begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

		vk::Rect2D render_area{
			.offset = {0, 0},
			.extent = {
				uint32_t(window.width()),
				uint32_t(window.height())
			}
		};

		vk::ClearValue clearColors[]{
			vk::ClearColorValue(std::array{0.0f, 0.0f, 0.0f, 1.0f}),
			vk::ClearDepthStencilValue{1.0f, 0}
		};

		vk::RenderPassBeginInfo beginInfo {
			.renderPass = _renderPass,
			.framebuffer = _framebuffers[_frameIndex],
			.renderArea = render_area,
			.clearValueCount = 2,
			.pClearValues = clearColors
		};

		_commandBuffers[_frameIndex].beginRenderPass(beginInfo, vk::SubpassContents::eInline);
		return _commandBuffers[_frameIndex];
	}

	void end() {
		_commandBuffers[_frameIndex].endRenderPass();
		_commandBuffers[_frameIndex].end();

		auto image_acquired_semaphore = _imageAcquiredSemaphore[_semaphoreIndex];
		auto render_complete_semaphore = _renderCompleteSemaphore[_semaphoreIndex];

		vk::PipelineStageFlags stages[] = {
			vk::PipelineStageFlagBits::eColorAttachmentOutput
		};

		vk::SubmitInfo submitInfo {
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &image_acquired_semaphore,
			.pWaitDstStageMask = stages,
			.commandBufferCount = 1,
			.pCommandBuffers = &_commandBuffers[_frameIndex],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &render_complete_semaphore
		};

		core->graphicsQueue().submit(1, &submitInfo, _fences[_frameIndex]);

		vk::PresentInfoKHR presentInfo {
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &render_complete_semaphore,
			.swapchainCount = 1,
			.pSwapchains = &_swapchain,
			.pImageIndices = &_frameIndex
		};
		core->presentQueue().presentKHR(presentInfo);

		_semaphoreIndex = (_semaphoreIndex + 1) % _frameCount;
	}

private:
	void handleWindowResize(int width, int height) {}

	void handleFramebufferResize(int width, int height) {}

	void handleIconify(int iconified) {}

	void keyCallback(int key, int scancode, int action, int mods) {
		_gui.keyCallback(key, scancode, action, mods);
		input.handleKeyInput(key, scancode, action, mods);
	}

	void mouseButtonCallback(int mouseButton, int action, int mods) {
		_gui.mouseButtonCallback(mouseButton, action, mods);
		input.handleMouseButton(mouseButton, action, mods);
	}

	void cursorPosCallback(double xpos, double ypos) {
		input.handleMousePosition(xpos, ypos);
	}

	void scrollCallback(double xoffset, double yoffset) {
		_gui.scrollCallback(xoffset, yoffset);
	}

	void charCallback(unsigned int c) {
		_gui.charCallback(c);
	}

private:
	GUI _gui;

	std::vector<Image> _depthImages;
	std::vector<vk::ImageView> _depthImageViews;

	std::vector<vk::Semaphore> _imageAcquiredSemaphore;
	std::vector<vk::Semaphore> _renderCompleteSemaphore;

	vk::RenderPass _renderPass;
	vk::Format _depthFormat;

	uint32_t _frameIndex = 0;
	uint32_t _frameCount = 0;
	uint32_t _semaphoreIndex = 0;

	vk::Extent2D _surface_extent;
	vk::SurfaceFormatKHR _surface_format;
	vk::PresentModeKHR _present_mode;

	vk::SwapchainKHR _swapchain;
	std::vector<vk::Image> _swapchainImages;
	std::vector<vk::ImageView> _swapchainImageViews;
	std::vector<CommandPool> _commandPools;
	std::vector<vk::CommandBuffer> _commandBuffers;
	std::vector<vk::Framebuffer> _framebuffers;
	std::vector<vk::Fence> _fences;
};

int main(int, char**) {
	Application app{};
	app.run();
	return 0;
}