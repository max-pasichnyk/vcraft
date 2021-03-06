#pragma once

#include "client/renderer/RenderSystem.hpp"
#include "client/util/DescriptorPool.hpp"

#include "client/renderer/texture/Texture.hpp"

struct Material {
	inline static constinit vk::PushConstantRange constants[] {
		{vk::ShaderStageFlagBits::eVertex, 0, sizeof(CameraTransform)}
	};

	RenderSystem* core = RenderSystem::Instance();

	vk::Sampler sampler;

	vk::DescriptorSetLayout descriptorSetLayout;
	vk::DescriptorSet descriptorSet;

	vk::PipelineLayout pipelineLayout;
	vk::Pipeline pipeline;

	Material(Handle<RenderContext> renderContext, std::span<const vk::PipelineShaderStageCreateInfo> shaderStages) {
		vk::SamplerCreateInfo samplerCreateInfo{
			.magFilter = vk::Filter::eNearest,
			.minFilter = vk::Filter::eNearest,
			.mipmapMode = vk::SamplerMipmapMode::eNearest,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			.addressModeW = vk::SamplerAddressMode::eRepeat,
			.maxAnisotropy = 0,
			.minLod = 0,
			.maxLod = 0
		};

		sampler = core->device().createSampler(samplerCreateInfo, nullptr);

		vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = &sampler
		};

		vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
				.bindingCount = 1,
				.pBindings = &descriptorSetLayoutBinding
		};
		descriptorSetLayout = core->device().createDescriptorSetLayout(descriptorSetLayoutCreateInfo, nullptr);
		descriptorSet = renderContext->descriptorPool.allocate(descriptorSetLayout);

		/*-----------------------------------------------------------------------------------------------------------*/

		vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &descriptorSetLayout,
				.pushConstantRangeCount = std::size(constants),
				.pPushConstantRanges = constants
		};
		pipelineLayout = core->device().createPipelineLayout(pipelineLayoutCreateInfo, nullptr);

		vk::VertexInputBindingDescription bindings[] {
				{0, sizeof(Vertex), vk::VertexInputRate::eVertex}
		};

		vk::VertexInputAttributeDescription attributes[]{
				{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)},
				{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)},
				{2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, coords)},
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

		vk::PipelineViewportStateCreateInfo viewportState{
				.viewportCount = 1,
				.pViewports = nullptr,
				.scissorCount = 1,
				.pScissors = nullptr
		};

		vk::PipelineRasterizationStateCreateInfo rasterizationState{
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eBack,
				.frontFace = vk::FrontFace::eClockwise,
				.lineWidth = 1.0f
		};

		vk::PipelineMultisampleStateCreateInfo multisampleState {};

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

		vk::DynamicState dynamicStates[] {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState {
			.dynamicStateCount = 2,
			.pDynamicStates = dynamicStates
		};

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo{
				.stageCount = static_cast<uint32_t>(std::size(shaderStages)),
				.pStages = std::data(shaderStages),
				.pVertexInputState = &vertexInputState,
				.pInputAssemblyState = &inputAssemblyState,
				.pViewportState = &viewportState,
				.pRasterizationState = &rasterizationState,
				.pMultisampleState = &multisampleState,
				.pDepthStencilState = &depthStencilState,
				.pColorBlendState = &colorBlendState,
				.pDynamicState = &dynamicState,
				.layout = pipelineLayout,
				.renderPass = renderContext->renderPass,
		};

		core->device().createGraphicsPipelines(nullptr, 1, &pipelineCreateInfo, nullptr, &pipeline);
	}

	void SetTexture(Texture* texture) {
		vk::DescriptorImageInfo imageInfo{
				.sampler = texture->renderTexture->sampler,
				.imageView = texture->renderTexture->view,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
		};

		vk::WriteDescriptorSet writeDescriptorSet{
				.dstSet = descriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = &imageInfo,
		};

		core->device().updateDescriptorSets(1, &writeDescriptorSet, 0, nullptr);
	}

	void destroy() {
		core->device().destroyDescriptorSetLayout(descriptorSetLayout, nullptr);
		core->device().destroyPipelineLayout(pipelineLayout, nullptr);
		core->device().destroyPipeline(pipeline, nullptr);
	}

private:

};