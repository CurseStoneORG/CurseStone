// CraftingManager.cpp — carga resources/recipes.json (formato Nukkit)
// y construye el índice de recetas.
//
// El proyecto no depende de ninguna librería JSON, así que acá vive un
// mini-parser JSON de objetivo único (suficiente para este archivo:
// objetos, arrays, strings, números, true/false/null). 338 recetas ×
// campos planos — el parseo entero tarda < 1 ms en hardware modesto.
#include "CraftingManager.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cctype>
#include <stdexcept>
namespace cursestone::inventory {
namespace {
// ============================================================
// Mini-parser JSON — devuelve el documento como variantes propias.
// ============================================================
struct JVal;
using JArray = std::vector<JVal>;
// Un vector de pares en lugar de unordered_map: JVal es un tipo
// recursivo (OBJ contiene JVals) y un map con tipo incompleto no
// compila. Los objetos de recipes.json tienen <= 6 claves, así que la
// búsqueda lineal es incluso más rápida que hashear.
struct JVal {
    enum Kind { NUL, NUM, BOOL, STR, ARR, OBJ } kind = NUL;
    double num = 0;
    bool b = false;
    std::string str;
    JArray arr;
    std::vector<std::pair<std::string, JVal>> obj;
    bool isNum() const { return kind == NUM; }
    bool isArr() const { return kind == ARR; }
    bool isObj() const { return kind == OBJ; }
    // get("k") devuelve nullptr si no es objeto o no existe la clave.
    const JVal* get(const std::string& k) const {
        if (kind != OBJ) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == k) return &kv.second;
        }
        return nullptr;
    }
};
class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}
    JVal parse() {
        JVal v = parseValue();
        skipWs();
        if (pos_ != s_.size()) fail("trailing garbage");
        return v;
    }
private:
    const std::string& s_;
    size_t pos_ = 0;
    [[noreturn]] void fail(const std::string& what) const {
        throw std::runtime_error(std::string("recipes.json: ") + what +
                                 " at byte " + std::to_string(pos_));
    }
    void skipWs() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' ||
                s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }
    char peek() {
        if (pos_ >= s_.size()) fail("unexpected end of input");
        return s_[pos_];
    }
    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }
    JVal parseValue() {
        skipWs();
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { JVal v; v.kind = JVal::STR; v.str = parseString(); return v; }
            case 't': case 'f': return parseBool();
            case 'n': return parseNull();
            default:  return parseNumber();
        }
    }
    JVal parseObject() {
        JVal v; v.kind = JVal::OBJ;
        expect('{'); skipWs();
        if (peek() == '}') { ++pos_; return v; }
        for (;;) {
            skipWs();
            std::string key = parseString();
            skipWs(); expect(':');
            v.obj.push_back({std::move(key), parseValue()});
            skipWs();
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == '}') { ++pos_; return v; }
            fail("expected ',' or '}' in object");
        }
    }
    JVal parseArray() {
        JVal v; v.kind = JVal::ARR;
        expect('['); skipWs();
        if (peek() == ']') { ++pos_; return v; }
        for (;;) {
            v.arr.push_back(parseValue());
            skipWs();
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == ']') { ++pos_; return v; }
            fail("expected ',' or ']' in array");
        }
    }
    std::string parseString() {
        expect('"');
        std::string out;
        while (pos_ < s_.size() && s_[pos_] != '"') {
            char c = s_[pos_++];
            if (c == '\\' && pos_ < s_.size()) {
                char e = s_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        // \uXXXX — recipes.json no usa non-ASCII, así que
                        // basta con consumir los 4 hex y mapear a '?'
                        // (los únicos escapes reales serían ASCII).
                        if (pos_ + 4 > s_.size()) fail("bad \\u escape");
                        pos_ += 4;
                        out.push_back('?');
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        expect('"');
        return out;
    }
    JVal parseBool() {
        if (s_.compare(pos_, 4, "true") == 0) { pos_ += 4; JVal v; v.kind = JVal::BOOL; v.b = true; return v; }
        if (s_.compare(pos_, 5, "false") == 0) { pos_ += 5; JVal v; v.kind = JVal::BOOL; v.b = false; return v; }
        fail("bad literal");
    }
    JVal parseNull() {
        if (s_.compare(pos_, 4, "null") != 0) fail("bad literal");
        pos_ += 4;
        return JVal{};  // kind = NUL
    }
    JVal parseNumber() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < s_.size() &&
               (std::isdigit((unsigned char)s_[pos_]) || s_[pos_] == '.' ||
                s_[pos_] == 'e' || s_[pos_] == 'E' ||
                s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
        if (pos_ == start) fail("bad number");
        JVal v; v.kind = JVal::NUM;
        v.num = std::stod(s_.substr(start, pos_ - start));
        return v;
    }
};
// Helpers de lectura con default.
int jInt(const JVal* v, int def) { return (v && v->isNum()) ? (int)v->num : def; }
// Item {ID, Count, Damage, NBT} del JSON -> ItemStack.
cursestone::item::ItemStack jItem(const JVal* v) {
    cursestone::item::ItemStack s;
    if (!v || !v->isObj()) return s;
    s.id    = (uint16_t)jInt(v->get("ID"), 0);
    s.count = jInt(v->get("Count"), 0);
    s.meta  = (uint8_t)(jInt(v->get("Damage"), 0) & 0xFF);
    return s;
}
// Meta cruda del JSON (-1 = any damage).
int16_t jMeta(const JVal* v) { return (int16_t)jInt(v->get("Damage"), -1); }
} // namespace
CraftingManager::CraftingManager() = default;

// ============================================================
// loadFromJson
// ============================================================
int CraftingManager::loadFromJson(const std::string& json_path) {
    std::ifstream f(json_path, std::ios::binary);
    if (!f.good()) return 0;
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();
    JVal root;
    try {
        root = JsonParser(src).parse();
    } catch (const std::exception& e) {
        // Un recipes.json roto no debe tumbar el arranque del server —
        // se loguea por stderr (el logger del server no llega acá
        // porque CraftingManager no conoce Server) y se sigue sin
        // recetas, igual que antes del fix.
        std::fprintf(stderr, "[CraftingManager] %s\n", e.what());
        return 0;
    }
    if (!root.isArr()) return 0;
    recipes_.clear();
    by_uuid_.clear();
    for (const auto& r : root.arr) {
        if (!r.isObj()) continue;
        int type = jInt(r.get("Type"), -1);
        const JVal* ing = r.get("Ingredients");
        if (!ing) continue;
        const JVal* resArr = r.get("Result");
        cursestone::item::ItemStack result;
        if (resArr && resArr->isArr() && !resArr->arr.empty()) {
            // shaped/shapeless: Result es una lista con un item.
            result = jItem(&resArr->arr.front());
        } else if (resArr) {
            // furnace: Result es un objeto item directo.
            result = jItem(resArr);
        }
        if (type == 1 && ing->isArr()) {
            // ---- Shaped: Ingredients = lista Width*Height, fila por
            // ---- fila, ID 0 = vacío.
            const JVal* wv = r.get("Width");
            const JVal* hv = r.get("Height");
            int w = jInt(wv, 0), h = jInt(hv, 0);
            if (w <= 0 || h <= 0) continue;
            size_t cells = (size_t)w * (size_t)h;
            if (ing->arr.size() != cells) continue;
            auto rec = std::make_unique<Recipe>(RECIPE_SHAPED, result);
            rec->setSize(w, h);
            std::array<uint16_t, 9> grid{};
            std::array<int16_t, 9> meta{};
            for (size_t row = 0; row < (size_t)h; ++row) {
                for (size_t col = 0; col < (size_t)w; ++col) {
                    const JVal& cell = ing->arr[row * w + col];
                    grid[row * 3 + col] = (uint16_t)jInt(cell.get("ID"), 0);
                    meta[row * 3 + col] = jMeta(&cell);
                }
            }
            rec->setGrid(grid);
            rec->setGridMeta(meta);
            recipes_.push_back(std::move(rec));
        } else if (type == 0 && ing->isArr()) {
            // ---- Shapeless: Ingredients = lista plana de items.
            if (ing->arr.empty()) continue;
            auto rec = std::make_unique<Recipe>(RECIPE_SHAPELESS, result);
            std::vector<uint16_t> ids;
            std::vector<int16_t> metas;
            ids.reserve(ing->arr.size());
            metas.reserve(ing->arr.size());
            for (const auto& cell : ing->arr) {
                ids.push_back((uint16_t)jInt(cell.get("ID"), 0));
                metas.push_back(jMeta(&cell));
            }
            // matchShapeless compara contra la lista ordenada.
            std::vector<size_t> order(ids.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b) { return ids[a] < ids[b]; });
            std::vector<uint16_t> sortedIds;
            std::vector<int16_t> sortedMetas;
            sortedIds.reserve(ids.size());
            sortedMetas.reserve(ids.size());
            for (size_t i : order) {
                sortedIds.push_back(ids[i]);
                sortedMetas.push_back(metas[i]);
            }
            rec->setIngredients(sortedIds);
            rec->setIngredientMeta(sortedMetas);
            recipes_.push_back(std::move(rec));
        } else if (type == 2 && ing && ing->isNum()) {
            // ---- Furnace: Ingredients es un NÚMERO escalar (el id de
            // entrada), no una lista. Damage opcional (default -1 =
            // any damage, ENTRY_FURNACE en el wire).
            auto rec = std::make_unique<Recipe>(RECIPE_FURNACE, result);
            rec->setIngredients({(uint16_t)(int16_t)ing->num});
            rec->setFurnaceInputMeta((int16_t)jInt(r.get("Damage"), -1));
            recipes_.push_back(std::move(rec));
        } else {
            // Type 3/5 (furnace-data legacy, multi-recipe) — ignoradas.
        }
    }
    // Asignar UUID deterministas + construir índice por bytes.
    uint64_t next = 1;
    for (auto& rec : recipes_) {
        rec->setUuid(next++);
        std::array<uint8_t,16> bytes{};
        uint64_t v = rec->uuid();
        for (int i = 0; i < 8; ++i) bytes[i]  = (uint8_t)(v >> (56 - i * 8));
        v = 0x9E3779B97F4A7C15ull * rec->uuid();  // segunda mitad distinta
        for (int i = 0; i < 8; ++i) bytes[8 + i] = (uint8_t)(v >> (56 - i * 8));
        rec->setUuidBytes(bytes);
        by_uuid_.emplace(bytes, rec.get());
    }
    return (int)recipes_.size();
}
// ============================================================
// Búsquedas
// ============================================================
namespace {
// ¿La caja W x H de la receta calza en la grilla 3x3 del jugador si la
// trasladamos al offset (dy, dx)? FUERA de la caja todo debe estar
// vacío; DENTRO, cada celda debe ser igual (ids) y pasar el chequeo de
// meta si aplica. Devuelve true si calza. `check_meta` puede ser null
// para comparar solo ids (matchShaped).
bool shapedFitsAt(const std::array<uint16_t, 9>& rg,
                  const std::array<int16_t, 9>& rm,
                  int w, int h,
                  const std::array<uint16_t, 9>& grid,
                  const std::array<uint16_t, 9>* grid_meta,
                  int dy, int dx) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            bool inBox = (i >= dy && i < dy + h && j >= dx && j < dx + w);
            if (!inBox) {
                if (grid[i * 3 + j] != 0) return false;  // sobra item
                continue;
            }
            uint16_t want = rg[(i - dy) * 3 + (j - dx)];
            uint16_t got = grid[i * 3 + j];
            if (got != want) return false;
            if (grid_meta) {
                int16_t wantMeta = rm[(i - dy) * 3 + (j - dx)];
                if (wantMeta >= 0 &&
                    (*grid_meta)[i * 3 + j] != (uint16_t)wantMeta) return false;
            }
        }
    }
    return true;
}
} // namespace

const Recipe* CraftingManager::matchShaped(const std::array<uint16_t, 9>& grid) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPED) continue;
        // "any damage" en alguna celda → match exacto por ids puede ser
        // un falso negativo si el jugador usó meta != 0; esas recetas
        // las resuelve matchShapedWithMeta.
        bool anyMeta = false;
        for (int16_t m : r->gridMeta()) if (m < 0) { anyMeta = true; break; }
        if (anyMeta) continue;
        // Búsqueda traslacional: la receta está escrita arriba-izquierda
        // en recipes.json, pero el jugador puede ponerla en cualquier
        // posición del 3x3.
        for (int dy = 0; dy + r->height() <= 3; ++dy) {
            for (int dx = 0; dx + r->width() <= 3; ++dx) {
                if (shapedFitsAt(r->grid(), r->gridMeta(), r->width(), r->height(),
                                 grid, nullptr, dy, dx))
                    return r.get();
            }
        }
    }
    return nullptr;
}
const Recipe* CraftingManager::matchShapedWithMeta(
        const std::array<uint16_t, 9>& grid,
        const std::array<uint16_t, 9>& grid_meta) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPED) continue;
        for (int dy = 0; dy + r->height() <= 3; ++dy) {
            for (int dx = 0; dx + r->width() <= 3; ++dx) {
                if (shapedFitsAt(r->grid(), r->gridMeta(), r->width(), r->height(),
                                 grid, &grid_meta, dy, dx))
                    return r.get();
            }
        }
    }
    return nullptr;
}
const Recipe* CraftingManager::matchShapeless(const std::vector<uint16_t>& ing) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPELESS) continue;
        bool anyMeta = false;
        for (int16_t m : r->ingredientMeta()) if (m < 0) { anyMeta = true; break; }
        if (anyMeta) continue;
        if (r->ingredients() == ing) return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::matchShapelessWithMeta(
        const std::vector<uint16_t>& ing,
        const std::vector<uint16_t>& meta) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPELESS) continue;
        const auto& ri = r->ingredients();
        const auto& rm = r->ingredientMeta();
        if (ri.size() != ing.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < ri.size(); ++i) {
            if (ri[i] != ing[i]) { ok = false; break; }
            if (i < rm.size() && rm[i] >= 0 && meta[i] != (uint16_t)rm[i]) { ok = false; break; }
        }
        if (ok) return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::matchFurnace(uint16_t input_id) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_FURNACE) continue;
        if (!r->ingredients().empty() && r->ingredients()[0] == input_id) return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::matchFurnaceWithMeta(uint16_t input_id, uint16_t meta) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_FURNACE) continue;
        if (r->ingredients().empty() || r->ingredients()[0] != input_id) continue;
        if (r->furnaceInputMeta() < 0 || (uint16_t)r->furnaceInputMeta() == meta)
            return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::getRecipeByUuidBytes(const uint8_t* uuid16) const {
    if (!uuid16) return nullptr;
    std::array<uint8_t,16> k{};
    for (int i = 0; i < 16; ++i) k[i] = uuid16[i];
    auto it = by_uuid_.find(k);
    return it == by_uuid_.end() ? nullptr : it->second;
}
std::vector<const Recipe*> CraftingManager::recipesByResult(
        uint16_t result_id, uint16_t result_meta) const {
    std::vector<const Recipe*> out;
    for (const auto& r : recipes_) {
        if (r->result().id == result_id &&
            (r->result().meta == result_meta || r->result().meta == 0))
            out.push_back(r.get());
    }
    return out;
}
} // namespace
