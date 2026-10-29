// CurseStone | vgen — minimal NBT value shim.
//
// The restored generator's structure pieces carry save/load methods
// (addAdditionalSaveData / readAdditionalSaveData) that never run in the
// server — population re-derives every structure deterministically from
// the seed. Those methods still have to COMPILE, so this stub provides
// just the tag surface they touch. No I/O, no persistence.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace vgen {

class CompoundTag;
class ListTag;

class Tag {
public:
	virtual ~Tag() = default;
};

class IntArrayTag : public Tag {
public:
	std::vector<int> mValues;
	std::vector<int>& data = mValues;
	IntArrayTag() = default;
	IntArrayTag(std::vector<int> v) : mValues(std::move(v)) {}
};

class CompoundTag : public Tag {
public:
	std::string mName;
	CompoundTag() = default;
	explicit CompoundTag(const std::string& name) : mName(name) {}
	bool contains(const std::string& k) const {
		return mInts.count(k) || mFloats.count(k) || mStrings.count(k) ||
		       mArrays.count(k) || mChildren.count(k) || mLists.count(k) ||
		       mOthers.count(k);
	}
	bool isEmpty() const {
		return mInts.empty() && mFloats.empty() && mStrings.empty() &&
		       mArrays.empty() && mChildren.empty() && mLists.empty() &&
		       mOthers.empty();
	}
	void putInt(const std::string& k, int v) { mInts[k] = v; }
	int getInt(const std::string& k, int def = 0) const {
		auto it = mInts.find(k);
		return it != mInts.end() ? it->second : def;
	}
	void putBoolean(const std::string& k, bool v) { mInts[k] = v ? 1 : 0; }
	bool getBoolean(const std::string& k, bool def = false) const {
		auto it = mInts.find(k);
		return it != mInts.end() ? it->second != 0 : def;
	}
	void putString(const std::string& k, const std::string& v) { mStrings[k] = v; }
	std::string getString(const std::string& k, const std::string& def = "") const {
		auto it = mStrings.find(k);
		return it != mStrings.end() ? it->second : def;
	}
	void putFloat(const std::string& k, float v) { mFloats[k] = v; }
	float getFloat(const std::string& k, float def = 0.0f) const {
		auto it = mFloats.find(k);
		return it != mFloats.end() ? it->second : def;
	}
	std::vector<int> getIntArray(const std::string& k) const {
		auto it = mArrays.find(k);
		return it != mArrays.end() ? it->second : std::vector<int>();
	}
	void putIntArray(const std::string& k, std::vector<int> v) { mArrays[k] = std::move(v); }
	void putCompound(const std::string& k, std::unique_ptr<CompoundTag> v) { mChildren[k] = std::move(v); }
	const CompoundTag* getCompound(const std::string& k) const {
		auto it = mChildren.find(k);
		return it != mChildren.end() ? it->second.get() : nullptr;
	}
	void put(const std::string& k, std::unique_ptr<ListTag> v) { mLists[k] = std::move(v); }
	template <typename T>
	void put(const std::string& k, std::unique_ptr<T> v) {
		mOthers[k].reset(v.release());
	}
	const ListTag* getList(const std::string& k) const {
		auto it = mLists.find(k);
		return it != mLists.end() ? it->second.get() : nullptr;
	}

private:
	std::unordered_map<std::string, int> mInts;
	std::unordered_map<std::string, float> mFloats;
	std::unordered_map<std::string, std::string> mStrings;
	std::unordered_map<std::string, std::vector<int>> mArrays;
	std::unordered_map<std::string, std::unique_ptr<CompoundTag>> mChildren;
	std::unordered_map<std::string, std::unique_ptr<ListTag>> mLists;
	std::unordered_map<std::string, std::unique_ptr<Tag>> mOthers;
};

class ListTag : public Tag {
public:
	std::vector<std::unique_ptr<Tag>> mTags;
	void add(std::unique_ptr<CompoundTag> t) { mTags.push_back(std::move(t)); }
	template <typename T>
	void add(std::unique_ptr<T> v) { mTags.emplace_back(v.release()); }
	size_t size() const { return mTags.size(); }
	const CompoundTag* get(int i) const {
		return static_cast<const CompoundTag*>(mTags[i].get());
	}
	const CompoundTag* getCompound(int i) const { return get(i); }
};

} // namespace vgen
