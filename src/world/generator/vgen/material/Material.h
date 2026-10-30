/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */

#pragma once

#include "MaterialType.h"
#include "vgen/Vanilla.h"

namespace vgen {

class Material {
public:

	enum class Settings {
		Normal,
		Gas,
		Liquid,
		Decoration,
		Portal
	};

	static const Material& getMaterial(MaterialType type);

	Material(MaterialType type, Settings settings = Settings::Normal, float translucency = 0.0f);

	Material(const Material&) = delete;
	Material& operator=(const Material&) = delete;

	static void addMaterial(std::unique_ptr<Material> mat);

	bool operator==(const Material& rhs) const;
	bool operator!=(const Material& rhs) const;

	static void initMaterials();
	static void teardownMaterials();

	~Material();
	
	bool isType(MaterialType surface) const;

	bool isSolidBlocking() const;
	
	bool isFlammable() const;
	bool isNeverBuildable() const;
	bool isAlwaysDestroyable() const;
	bool isReplaceable() const;
	bool isLiquid() const;
	float getTranslucency() const;
	bool getBlocksMotion() const;
	bool isSolid() const;
	bool isSuperHot() const;

	Color getColor() const;

protected:

	Material& _setReplaceable();
	Material& _setFlammable();
	Material& _setNotAlwaysDestroyable();
	Material& _setNeverBuildable();
	Material& _setNotBlockingMotion();
	Material& _setNotSolid();
	Material& _setSuperHot();

private:

	static void _setupSurfaceMaterials();

	void _setMapColor(const Color& color);

	static bool mInitialized;
	
	MaterialType mType;
	bool mFlammable;
	bool mNeverBuildable;
	bool mAlwaysDestroyable;
	bool mReplaceable;
	bool mLiquid;
	float mTranslucency;
	bool mBlocksMotion;
	bool mSolid;
	bool mSuperHot = false;
	Color mMaterialColor;

	static std::vector<std::unique_ptr<Material> > mMaterials;
};

} // namespace vgen

