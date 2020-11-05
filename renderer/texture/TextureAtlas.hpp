#pragma once

#include "util/math/Rect2D.hpp"

#include "renderer/TextureUVCoordinateSet.hpp"

#include "NativeImage.hpp"

#include <string>
#include <vector>
#include <map>

struct TextureAtlasSprite {
	struct Info {
		std::string path;
		NativeImage image;

		void *pixels() const {
			return image.pixels;
		}

		int width() const {
			return image.width;
		}

		int height() const {
			return image.height;
		}
	};

	Info info;
	int originX;
	int originY;

	TextureAtlasSprite(Info info, int originX, int originY)
		: info(std::move(info)), originX(originX), originY(originY) {}
};

struct SheetData {
	std::vector<TextureAtlasSprite> sprites;
	int width;
	int height;

	SheetData(std::vector<TextureAtlasSprite>&& sprites, int width, int height)
		: sprites(std::move(sprites))
		, width(width)
		, height(height) {}
};

struct TextureAtlasPack {
	struct Holder {
		TextureAtlasSprite::Info info;
		int width;
		int height;

		Holder(TextureAtlasSprite::Info info)
			: info(std::move(info))
			, width(info.width())
			, height(info.height()) {}
	};

	struct Slot {
		Slot() = default;

		Slot(int x, int y, int w, int h) : rect{x, y, w, h} {}

		bool canFit(int width, int height) {
			return width <= rect.width && height <= rect.height;
		}

		bool addSlot(Holder* holderIn) {
			if (canFit(holderIn->width, holderIn->height)) {
				if (holder == nullptr) {
					holder = holderIn;

					int originX = rect.x;
					int originY = rect.y;
					int w = holder->width;
					int h = holder->height;

					if (rect.width > w) {
						subSlots.emplace_back(new Slot(originX + w, originY, rect.width - w, h));
					}
					if (rect.height > h) {
						subSlots.emplace_back(new Slot(originX, originY + h, rect.width, rect.height - h));
					}

					return true;
				}

				for (auto& slot : subSlots) {
					if (slot->addSlot(holderIn)) {
						return true;
					}
				}
			}
			return false;
		}

		template <typename Fn>
		void getAllStitchSlots(Fn&& fn) {
			if (holder != nullptr) {
				fn(this);
			}

			for (auto& slot : subSlots) {
				slot->getAllStitchSlots(std::forward<Fn>(fn));
			}
		}

		std::vector<std::unique_ptr<Slot>> subSlots;
		Holder* holder = nullptr;
		Rect2D rect;
	};

	void addSprite(const TextureAtlasSprite::Info& info) {
		holders.emplace_back(std::make_unique<Holder>(info));
	}

	bool expandAndAllocateSlot(Holder* holder) {
		Slot* slot = nullptr;

		int textureSize = std::max(
			/*nextPowerOf2*/(currentWidth + holder->width),
			/*nextPowerOf2*/(currentHeight + holder->height)
		);

		slots.reserve(2);
		slots.emplace_back(new Slot(currentWidth, 0, textureSize - currentWidth, currentHeight));
		if (slots.back()->canFit(holder->width, holder->height)) {
			slot = slots.back().get();
		}
		slots.emplace_back(new Slot(0, currentHeight, textureSize, textureSize - currentHeight));
		if (!slot && slots.back()->canFit(holder->width, holder->height)) {
			slot = slots.back().get();
		}
		currentWidth = textureSize;
		currentHeight = textureSize;

		return slot->addSlot(holder);
	}

	bool allocateSlot(Holder* holder) {
		for (auto& slot : slots) {
			if (slot->addSlot(holder)) {
				return true;
			}
		}

		return expandAndAllocateSlot(holder);
	}

	std::optional<SheetData> build() {
		std::sort(holders.begin(), holders.end(), [](auto& b, auto& a) -> bool {
			return a->width < b->width || (a->width == b->width) && (a->height < b->height);
		});

		int textureSize = std::max(
			nextPowerOf2(holders[0]->width),
			nextPowerOf2(holders[0]->height)
		);
		currentWidth = textureSize;
		currentHeight = textureSize;

		slots.emplace_back(new Slot(0, 0, textureSize, textureSize));
		for (auto&& holder : holders) {
			if (!allocateSlot(holder.get())) {
				return std::nullopt;
			}
		}

		std::vector<TextureAtlasSprite> sprites;
		getAllSlots([&sprites](Slot *slot) {
			sprites.emplace_back(std::move(slot->holder->info), slot->rect.x, slot->rect.y);
		});

		return SheetData(std::move(sprites), currentWidth, currentHeight);
	}

	template <typename Fn>
	void getAllSlots(Fn&& fn) {
		for (auto&& slot : slots) {
			slot->getAllStitchSlots(std::forward<Fn>(fn));
		}
	}

	inline static constexpr unsigned nextPowerOf2(unsigned n) noexcept {
		n--;
		n |= n >> 1u;
		n |= n >> 2u;
		n |= n >> 4u;
		n |= n >> 8u;
		n |= n >> 16u;
		n++;
		return n;
	}

private:
	std::vector<std::unique_ptr<Holder>> holders;
	std::vector<std::unique_ptr<Slot>> slots;

	int currentWidth;
	int currentHeight;
};

struct TextureAtlasTextureItem {
	std::string name;
	std::vector<TextureUVCoordinateSet> textures;

	const TextureUVCoordinateSet& operator[](int i) const {
		return textures[i];
	}

	const TextureUVCoordinateSet& get(int i) const {
		return textures[i];
	}
};

struct ParsedAtlasNodeElement {
	std::string path;
//	std::optional<rgb24> overlay_color;
//	std::optional<rgb24> tint_color;
};

struct ParsedAtlasNode {
	std::string name;

	int quad;
	std::vector<ParsedAtlasNodeElement> elements;
};

struct TextureAtlas : Texture {
	std::optional<SheetData> sheet;
	std::string texture_name;
	int padding;
	int num_mip_levels;

	std::map<std::string, TextureAtlasSprite> sprites;
//	TextureAtlasTextureItem* pMissingTexture;

	std::map<std::string, TextureAtlasTextureItem> items;
	std::set<std::string> requireTextures;

	void _readElement(json::Value& value, ParsedAtlasNodeElement& element) {
		if (auto path = value.as_string()) {
			element.path = *path;
		} else {
			element.path = value.get("path").value().as_string().value();
//			if (auto overlay_color = value.get("overlay_color")) {
//				element.overlay_color = overlay_color->as_string();
//			}
//			if (auto tint_color = value.get("tint_color")) {
//				element.tint_color = tint_color->as_string().value();
//			}
		}
	}

	void _readNode(json::Value& value, ParsedAtlasNode& node) {
		if (auto quad = value.get("quad")) {
			node.quad = quad->as_i64().value();
		}

		auto textures = value.get("textures").value();
		if (auto list = textures.as_array()) {
			ParsedAtlasNodeElement element;
			for (auto& item : list.value()) {
				_readElement(item, element);

				requireTextures.emplace(element.path);
			}
			node.elements.emplace_back(element);
		} else {
			ParsedAtlasNodeElement element;
			_readElement(textures, element);
			node.elements.emplace_back(element);

			requireTextures.emplace(element.path);
		}
	}

	void _loadAtlasNodes(json::Object& texture_data, std::vector<ParsedAtlasNode>& nodes) {
		for (auto&& [name, data] : texture_data) {
			ParsedAtlasNode node{ .name = name };
			_readNode(data, node);
			nodes.emplace_back(std::move(node));
		}
	}

	void loadMetaFile(ResourceManager* resourceManager) {
		auto bytes = resourceManager->loadFile("textures/terrain_texture.json");
		auto object = json::Parser{bytes.value()}.parse().value().as_object().value();

		auto resource_pack_name = object.at("resource_pack_name").as_string().value();
		texture_name = object.at("texture_name").as_string().value();
		padding = object.at("padding").as_i64().value();
		num_mip_levels = object.at("num_mip_levels").as_i64().value();

		auto texture_data = object.at("texture_data").as_object().value();

		std::vector<ParsedAtlasNode> nodes;
		_loadAtlasNodes(texture_data, nodes);

		TextureAtlasPack textureAtlasPack;
		for (auto &path : requireTextures) {
			textureAtlasPack.addSprite({path, resourceManager->loadTextureData(path).value()});
		}
		sheet = textureAtlasPack.build();

		for (auto &sprite : sheet->sprites) {
			sprites.emplace(sprite.info.path, sprite);
		}

//		for (auto &node : nodes) {
//			TextureAtlasTextureItem item;
//			item.textures.reserve(node.elements.size());
//
//			for (auto &element : node.elements) {
//				auto sprite = sprites.at(element.path);
//
//				Rect2D rect{
//						sprite.originX,
//						sprite.originY,
//						sprite.info.width(),
//						sprite.info.height()
//				};
//
//				auto minU = float(rect.x) / float(sheet->width);
//				auto minV = float(rect.y) / float(sheet->height);
//				auto maxU = float(rect.x + rect.width) / float(sheet->width);
//				auto maxV = float(rect.y + rect.height) / float(sheet->height);
//
//				TextureUVCoordinateSet textureUvCoordinateSet{
//						.minU = minU,
//						.minV = minV,
//						.maxU = maxU,
//						.maxV = maxV
//				};
//
//				item.textures.emplace_back(textureUvCoordinateSet);
//			}
//
//			items.try_emplace(node.name, std::move(item));
//		}
	}

	TextureAtlasTextureItem& getTextureItem(const std::string& name) {
		return items.at(name);
	}

	void loadTexture(TextureManager* textureManager) override {
		std::vector<uint8_t> pixels{};
		pixels.resize(sheet->width * sheet->height * 4);
		for (auto& sprite : sheet->sprites) {
			Rect2D rect {
				sprite.originX,
				sprite.originY,
				sprite.info.width(),
				sprite.info.height()
			};

			int i2 = 0;
			for (int y = rect.y; y < rect.y + rect.height; y++) {
				auto i = (y * sheet->width + rect.x) * 4;

				for (int x = 0; x < rect.width; x++) {
					pixels[i++] = reinterpret_cast<unsigned char*>(sprite.info.pixels())[i2++];
					pixels[i++] = reinterpret_cast<unsigned char*>(sprite.info.pixels())[i2++];
					pixels[i++] = reinterpret_cast<unsigned char*>(sprite.info.pixels())[i2++];
					pixels[i++] = reinterpret_cast<unsigned char*>(sprite.info.pixels())[i2++];
				}
			}
		}

		renderTexture = textureManager->createTexture(vk::Format::eR8G8B8A8Unorm, sheet->width, sheet->height, 4, pixels.data());
	}
};
