#include "Horo/Gameplay/LuaBehavior.h"

#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <type_traits>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace Horo::Gameplay {
    namespace {
        struct LuaBudget {
            std::size_t used{};
            std::size_t maximum{};
        };

        // lua_Alloc contract: every block handed out is later returned through this same function,
        // so new[]/delete[] symmetry via unique_ptr preserves ownership without the malloc family.
        void *BudgetAllocate(void *userData, void *pointer, const std::size_t oldSize, const std::size_t newSize) {
            auto &budget = *static_cast<LuaBudget *>(userData);
            if (newSize == 0) {
                budget.used = oldSize > budget.used ? 0 : budget.used - oldSize;
                std::unique_ptr<std::byte[]> released{static_cast<std::byte *>(pointer)};
                return nullptr;
            }
            const std::size_t retained = oldSize > budget.used ? 0 : budget.used - oldSize;
            if (newSize > budget.maximum || retained > budget.maximum - newSize)
                return nullptr;
            std::unique_ptr<std::byte[]> resized;
            try {
                resized = std::make_unique_for_overwrite<std::byte[]>(newSize);
            } catch (const std::bad_alloc &) {
                return nullptr;
            }
            // Adopting the incoming block restores realloc's free-old-block semantics; on
            // allocation failure above it stays owned by Lua, matching the original contract.
            std::unique_ptr<std::byte[]> released{static_cast<std::byte *>(pointer)};
            if (pointer != nullptr && oldSize > 0)
                std::memcpy(resized.get(), pointer, std::min(oldSize, newSize));
            budget.used = retained + newSize;
            return resized.release();
        }

        void InstructionLimitHook(lua_State *state, lua_Debug *) {
            luaL_error(state, "Lua behavior instruction budget exceeded");
        }

        int IdentityBehavior(lua_State *state) {
            luaL_checktype(state, 1, LUA_TTABLE);
            lua_settop(state, 1);
            return 1;
        }

        void OpenSandbox(lua_State *state) {
            luaL_requiref(state, "_G", luaopen_base, 1);
            lua_pop(state, 1);
            luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
            lua_pop(state, 1);
            luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
            lua_pop(state, 1);
            luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
            lua_pop(state, 1);
            luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
            lua_pop(state, 1);
            for (const char *name : {"dofile", "loadfile", "collectgarbage", "io", "os", "package", "debug"}) {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }
            lua_newtable(state);
            lua_pushcfunction(state, IdentityBehavior);
            lua_setfield(state, -2, "behavior");
            lua_setglobal(state, "horo");
        }

        [[nodiscard]] Result<void> LuaFailure(const std::string &message) {
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent, message));
        }

        [[nodiscard]] BehaviorFieldValue ReadDefault(lua_State *state, const int index) {
            switch (lua_type(state, index)) {
                case LUA_TBOOLEAN:
                    return lua_toboolean(state, index) != 0;
                case LUA_TNUMBER:
                    if (lua_isinteger(state, index))
                        return static_cast<std::int64_t>(lua_tointeger(state, index));
                    return static_cast<double>(lua_tonumber(state, index));
                case LUA_TSTRING:
                    return std::string{lua_tostring(state, index)};
                default:
                    return std::monostate{};
            }
        }

        [[nodiscard]] Result<BehaviorDescriptor> ReadDescriptor(lua_State *state, const BehaviorTypeId &canonicalTypeId) {
            if (!lua_istable(state, -1))
                return Result<BehaviorDescriptor>::Failure(
                    MakeError(GameplayErrors::InvalidBehaviorComponent, "Lua behavior source must return a descriptor table."));
            BehaviorDescriptor descriptor;
            descriptor.typeId = canonicalTypeId;
            lua_getfield(state, -1, "type_id");
            if (!lua_isnil(state, -1) && (!lua_isstring(state, -1) || canonicalTypeId.Value() != lua_tostring(state, -1)))
                return Result<BehaviorDescriptor>::Failure(
                    MakeError(GameplayErrors::InvalidBehaviorComponent, "Lua source type_id does not match its sidecar identity."));
            lua_pop(state, 1);
            lua_getfield(state, -1, "display_name");
            if (!lua_isstring(state, -1))
                return Result<BehaviorDescriptor>::Failure(
                    MakeError(GameplayErrors::InvalidBehaviorComponent, "Lua behavior requires display_name."));
            descriptor.displayName = lua_tostring(state, -1);
            lua_pop(state, 1);
            lua_getfield(state, -1, "category");
            if (lua_isstring(state, -1))
                descriptor.category = lua_tostring(state, -1);
            lua_pop(state, 1);
            lua_getfield(state, -1, "schema_version");
            if (lua_isinteger(state, -1))
                descriptor.schemaVersion = static_cast<std::uint32_t>(lua_tointeger(state, -1));
            lua_pop(state, 1);
            lua_getfield(state, -1, "allow_multiple");
            descriptor.allowMultiple = lua_toboolean(state, -1) != 0;
            lua_pop(state, 1);
            lua_getfield(state, -1, "fields");
            if (lua_istable(state, -1)) {
                const lua_Integer count = luaL_len(state, -1);
                if (count < 0 || count > static_cast<lua_Integer>(MaximumBehaviorFields))
                    return Result<BehaviorDescriptor>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));
                for (lua_Integer index = 1; index <= count; ++index) {
                    lua_geti(state, -1, index);
                    if (!lua_istable(state, -1))
                        return Result<BehaviorDescriptor>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));
                    lua_getfield(state, -1, "name");
                    if (!lua_isstring(state, -1))
                        return Result<BehaviorDescriptor>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));
                    std::string name = lua_tostring(state, -1);
                    lua_pop(state, 1);
                    lua_getfield(state, -1, "default");
                    BehaviorFieldValue defaultValue = ReadDefault(state, -1);
                    lua_pop(state, 1);
                    descriptor.fields.emplace_back(std::move(name), std::move(defaultValue));
                    lua_pop(state, 1);
                }
            }
            lua_pop(state, 1);
            descriptor.phases.push_back({BehaviorPhase::Gameplay, canonicalTypeId.Value(), {}, {}, {}});
            return Result<BehaviorDescriptor>::Success(std::move(descriptor));
        }

        struct ParsedProgram {
            BehaviorDescriptor descriptor;
        };

        [[nodiscard]] Result<ParsedProgram> ParseProgram(std::string_view source, const BehaviorTypeId &canonicalTypeId,
                                                         const std::string &sourceName, const LuaBehaviorLimits limits) {
            LuaBudget budget{0, limits.maximumMemoryBytes};
            lua_State *state = lua_newstate(BudgetAllocate, &budget);
            if (state == nullptr)
                return Result<ParsedProgram>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent, "Unable to create Lua VM."));
            OpenSandbox(state);
            lua_sethook(state, InstructionLimitHook, LUA_MASKCOUNT, static_cast<int>(limits.maximumInstructionsPerCallback));
            if (const int loaded = luaL_loadbufferx(state, source.data(), source.size(), sourceName.c_str(), "t");
                loaded != LUA_OK || lua_pcall(state, 0, 1, 0) != LUA_OK) {
                const std::string message = lua_isstring(state, -1) ? lua_tostring(state, -1) : "Lua script compilation failed.";
                lua_close(state);
                return Result<ParsedProgram>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent, message));
            }
            auto descriptor = ReadDescriptor(state, canonicalTypeId);
            lua_close(state);
            if (descriptor.HasError())
                return Result<ParsedProgram>::Failure(descriptor.ErrorValue());
            return Result<ParsedProgram>::Success({std::move(descriptor).Value()});
        }

        [[nodiscard]] bool Compatible(const BehaviorDescriptor &active, const BehaviorDescriptor &candidate) {
            if (active.typeId != candidate.typeId || active.schemaVersion != candidate.schemaVersion ||
                active.allowMultiple != candidate.allowMultiple || active.fields.size() != candidate.fields.size() ||
                active.phases.size() != candidate.phases.size())
                return false;
            for (std::size_t index = 0; index < active.fields.size(); ++index) {
                if (active.fields[index].name != candidate.fields[index].name ||
                    active.fields[index].defaultValue.index() != candidate.fields[index].defaultValue.index())
                    return false;
            }
            return true;
        }
    }  // namespace

    struct LuaBehaviorProgram::Impl {
        BehaviorDescriptor descriptor;
        std::string source;
        std::string sourceName;
        LuaBehaviorLimits limits;
        std::uint64_t revision{1};
    };

    class LuaBehaviorInstance final : public IBehaviorInstance {
    public:
        explicit LuaBehaviorInstance(LuaBehaviorProgram &program) : program_(&program) {}

        LuaBehaviorInstance(const LuaBehaviorInstance &) = delete;
        LuaBehaviorInstance &operator=(const LuaBehaviorInstance &) = delete;
        LuaBehaviorInstance(LuaBehaviorInstance &&) = delete;
        LuaBehaviorInstance &operator=(LuaBehaviorInstance &&) = delete;

        ~LuaBehaviorInstance() override {
            Close();
        }

        void OnCreate(BehaviorContext &context) override {
            Call("on_create", context);
        }

        void OnEnable(BehaviorContext &context) override {
            Call("on_enable", context);
        }

        void OnStart(BehaviorContext &context) override {
            Call("on_start", context);
        }

        void OnInputAction(BehaviorContext &context, const GameplayInputAction &action) override {
            Call("on_input_action", context, std::nullopt, &action);
        }

        void OnEvent(BehaviorContext &context, const GameplayEvent &event) override {
            Call("on_event", context, std::nullopt, nullptr, &event);
        }

        void OnFixedUpdate(BehaviorContext &context, const FixedDeltaTime delta) override {
            Call("on_fixed_update", context, delta.seconds);
        }

        void OnPresentationUpdate(BehaviorContext &context, const FrameDeltaTime delta) override {
            Call("on_presentation_update", context, delta.seconds);
        }

        void OnDisable(BehaviorContext &context) override {
            Call("on_disable", context);
        }

        void OnDestroy(BehaviorContext &context) override {
            Call("on_destroy", context);
        }

    private:
        struct Vm {
            LuaBudget budget;
            lua_State *state{};
            int behaviorRef{LUA_NOREF};
            std::uint64_t revision{};
        };

        static BehaviorContext &Context(lua_State *state) {
            return *static_cast<BehaviorContext *>(lua_touserdata(state, lua_upvalueindex(1)));
        }

        static int Position(lua_State *state) {
            auto transform = Context(state).LocalTransform();
            if (transform.HasError())
                return luaL_error(state, "entity transform is unavailable");
            lua_pushnumber(state, transform.Value().translation.x);
            lua_pushnumber(state, transform.Value().translation.y);
            lua_pushnumber(state, transform.Value().translation.z);
            return 3;
        }

        static int SetPosition(lua_State *state) {
            auto transform = Context(state).LocalTransform();
            if (transform.HasError())
                return luaL_error(state, "entity transform is unavailable");
            Math::Transform changed = transform.Value();
            changed.translation = {static_cast<float>(luaL_checknumber(state, 1)), static_cast<float>(luaL_checknumber(state, 2)),
                                   static_cast<float>(luaL_checknumber(state, 3))};
            if (Context(state).SetLocalTransform(changed).HasError())
                return luaL_error(state, "transform mutation was rejected");
            return 0;
        }

        static int Action(lua_State *state) {
            const std::string_view requested = luaL_checkstring(state, 1);
            for (const GameplayInputAction &action : Context(state).InputActions()) {
                if (action.action.Value() != requested)
                    continue;
                lua_pushnumber(state, action.x);
                lua_pushnumber(state, action.y);
                lua_pushboolean(state, action.down);
                lua_pushboolean(state, action.pressed);
                lua_pushboolean(state, action.released);
                return 5;
            }
            lua_pushnumber(state, 0);
            lua_pushnumber(state, 0);
            lua_pushboolean(state, false);
            lua_pushboolean(state, false);
            lua_pushboolean(state, false);
            return 5;
        }

        static int Publish(lua_State *state) {
            if (Context(state).Publish(GameplayEvent{GameplayEventTypeId{luaL_checkstring(state, 1)}, 1, std::nullopt, {}}).HasError())
                return luaL_error(state, "event publication was rejected");
            return 0;
        }

        static int Field(lua_State *state) {
            const std::string_view name = luaL_checkstring(state, 1);
            for (const BehaviorField &field : Context(state).Fields()) {
                if (field.name != name)
                    continue;
                std::visit([state]<typename T>(const T &value) {
                    if constexpr (std::is_same_v<T, std::monostate>)
                        lua_pushnil(state);
                    else if constexpr (std::is_same_v<T, bool>)
                        lua_pushboolean(state, value);
                    else if constexpr (std::is_same_v<T, std::int64_t>)
                        lua_pushinteger(state, static_cast<lua_Integer>(value));
                    else if constexpr (std::is_same_v<T, double>)
                        lua_pushnumber(state, value);
                    else if constexpr (std::is_same_v<T, std::string>)
                        lua_pushlstring(state, value.data(), value.size());
                    else
                        lua_pushnil(state);
                }, field.value);
                return 1;
            }
            lua_pushnil(state);
            return 1;
        }

        static int LogInfo(lua_State *state) {
            LOG_INFO("gameplay.lua", "%s", luaL_checkstring(state, 1));
            return 0;
        }

        template <lua_CFunction Callback> static void Function(lua_State *state, BehaviorContext &context, const char *name) {
            lua_pushlightuserdata(state, &context);
            lua_pushcclosure(state, Callback, 1);
            lua_setfield(state, -2, name);
        }

        static void PushContext(lua_State *state, BehaviorContext &context) {
            lua_newtable(state);
            lua_newtable(state);
            Function<Position>(state, context, "position");
            Function<SetPosition>(state, context, "set_position");
            lua_setfield(state, -2, "transform");
            lua_newtable(state);
            Function<Action>(state, context, "action");
            lua_setfield(state, -2, "input");
            lua_newtable(state);
            Function<Publish>(state, context, "publish");
            lua_setfield(state, -2, "events");
            lua_newtable(state);
            Function<Field>(state, context, "get");
            lua_setfield(state, -2, "fields");
            lua_newtable(state);
            Function<LogInfo>(state, context, "info");
            lua_setfield(state, -2, "log");
            const GameplayEntityRef entity = context.Entity();
            lua_newtable(state);
            lua_pushinteger(state, entity.index);
            lua_setfield(state, -2, "index");
            lua_pushinteger(state, entity.generation);
            lua_setfield(state, -2, "generation");
            lua_setfield(state, -2, "entity");
        }

        void Close() noexcept {
            if (vm_.state != nullptr)
                lua_close(vm_.state);
            vm_ = {};
        }

        [[nodiscard]] bool EnsureLoaded() {
            if (failed_ && failedRevision_ == program_->Revision())
                return false;
            if (failed_) {
                failed_ = false;
                Close();
            }
            if (vm_.state != nullptr && vm_.revision == program_->Revision())
                return true;
            Close();
            const auto &impl = *program_->impl_;
            vm_.budget = {0, impl.limits.maximumMemoryBytes};
            vm_.state = lua_newstate(BudgetAllocate, &vm_.budget);
            if (vm_.state == nullptr)
                return Fail("Unable to create Lua behavior VM.");
            OpenSandbox(vm_.state);
            lua_sethook(vm_.state, InstructionLimitHook, LUA_MASKCOUNT, static_cast<int>(impl.limits.maximumInstructionsPerCallback));
            if (luaL_loadbufferx(vm_.state, impl.source.data(), impl.source.size(), impl.sourceName.c_str(), "t") != LUA_OK ||
                lua_pcall(vm_.state, 0, 1, 0) != LUA_OK || !lua_istable(vm_.state, -1))
                return Fail(lua_isstring(vm_.state, -1) ? lua_tostring(vm_.state, -1) : "Lua behavior load failed.");
            vm_.behaviorRef = luaL_ref(vm_.state, LUA_REGISTRYINDEX);
            vm_.revision = program_->Revision();
            return true;
        }

        bool Fail(const std::string &message) {
            LOG_ERROR("gameplay.lua", "%s", message.c_str());
            failed_ = true;
            failedRevision_ = program_->Revision();
            return false;
        }

        static void PushAction(lua_State *state, const GameplayInputAction &action) {
            lua_newtable(state);
            lua_pushlstring(state, action.action.Value().data(), action.action.Value().size());
            lua_setfield(state, -2, "id");
            lua_pushnumber(state, action.x);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, action.y);
            lua_setfield(state, -2, "y");
            lua_pushboolean(state, action.down);
            lua_setfield(state, -2, "down");
            lua_pushboolean(state, action.pressed);
            lua_setfield(state, -2, "pressed");
            lua_pushboolean(state, action.released);
            lua_setfield(state, -2, "released");
        }

        static void PushEvent(lua_State *state, const GameplayEvent &event) {
            lua_newtable(state);
            lua_pushlstring(state, event.type.Value().data(), event.type.Value().size());
            lua_setfield(state, -2, "type_id");
            lua_pushinteger(state, event.schemaVersion);
            lua_setfield(state, -2, "schema_version");
            lua_newtable(state);
            for (const BehaviorField &field : event.fields) {
                std::visit([state]<typename T>(const T &value) {
                    if constexpr (std::is_same_v<T, bool>)
                        lua_pushboolean(state, value);
                    else if constexpr (std::is_same_v<T, std::int64_t>)
                        lua_pushinteger(state, static_cast<lua_Integer>(value));
                    else if constexpr (std::is_same_v<T, double>)
                        lua_pushnumber(state, value);
                    else if constexpr (std::is_same_v<T, std::string>)
                        lua_pushlstring(state, value.data(), value.size());
                    else
                        lua_pushnil(state);
                }, field.value);
                lua_setfield(state, -2, field.name.c_str());
            }
            lua_setfield(state, -2, "fields");
        }

        void Call(const char *name, BehaviorContext &context, const std::optional<double> delta = std::nullopt,
                  const GameplayInputAction *action = nullptr, const GameplayEvent *event = nullptr) {
            if (!EnsureLoaded())
                return;
            lua_State *state = vm_.state;
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm_.behaviorRef);
            lua_getfield(state, -1, name);
            if (lua_isnil(state, -1)) {
                lua_pop(state, 2);
                return;
            }
            PushContext(state, context);
            int argumentCount = 1;
            if (delta.has_value()) {
                lua_pushnumber(state, *delta);
                ++argumentCount;
            } else if (action != nullptr) {
                PushAction(state, *action);
                ++argumentCount;
            } else if (event != nullptr) {
                PushEvent(state, *event);
                ++argumentCount;
            }
            if (lua_pcall(state, argumentCount, 0, 0) != LUA_OK) {
                const std::string message = lua_isstring(state, -1) ? lua_tostring(state, -1) : "Lua callback failed.";
                lua_pop(state, 1);
                Fail(message);
            }
            lua_pop(state, 1);
        }

        LuaBehaviorProgram *program_{};
        Vm vm_{};
        bool failed_{};
        std::uint64_t failedRevision_{};
    };

    LuaBehaviorProgram::LuaBehaviorProgram(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    LuaBehaviorProgram::~LuaBehaviorProgram() = default;

    /** @copydoc LuaBehaviorProgram::Compile */
    Result<std::unique_ptr<LuaBehaviorProgram>> LuaBehaviorProgram::Compile(std::string source, const BehaviorTypeId &canonicalTypeId,
                                                                            std::string sourceName, const LuaBehaviorLimits limits) {
        if (source.empty() || source.size() > 2U * 1024U * 1024U || !canonicalTypeId.IsValid() || limits.maximumMemoryBytes < 64U * 1024U ||
            limits.maximumInstructionsPerCallback == 0 ||
            limits.maximumInstructionsPerCallback > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            return Result<std::unique_ptr<LuaBehaviorProgram>>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));
        auto parsed = ParseProgram(source, canonicalTypeId, sourceName, limits);
        if (parsed.HasError())
            return Result<std::unique_ptr<LuaBehaviorProgram>>::Failure(parsed.ErrorValue());
        auto impl = std::make_unique<Impl>();
        impl->descriptor = std::move(parsed).Value().descriptor;
        impl->source = std::move(source);
        impl->sourceName = std::move(sourceName);
        impl->limits = limits;
        return Result<std::unique_ptr<LuaBehaviorProgram>>::Success(std::make_unique<LuaBehaviorProgram>(std::move(impl)));
    }

    /** @copydoc LuaBehaviorProgram::LoadFiles */
    Result<std::unique_ptr<LuaBehaviorProgram>> LuaBehaviorProgram::LoadFiles(const std::filesystem::path &sourcePath,
                                                                              const std::filesystem::path &sidecarPath,
                                                                              const LuaBehaviorLimits limits) {
        std::error_code error;
        if (const auto sourceSize = std::filesystem::file_size(sourcePath, error);
            error || sourceSize == 0 || sourceSize > 2U * 1024U * 1024U)
            return Result<std::unique_ptr<LuaBehaviorProgram>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, "Lua behavior source is missing or oversized."));
        if (const auto sidecarSize = std::filesystem::file_size(sidecarPath, error); error || sidecarSize == 0 || sidecarSize > 64U * 1024U)
            return Result<std::unique_ptr<LuaBehaviorProgram>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, "Lua behavior sidecar is missing or oversized."));
        std::ifstream sourceInput(sourcePath, std::ios::binary);
        std::ifstream sidecarInput(sidecarPath, std::ios::binary);
        std::ostringstream source;
        source << sourceInput.rdbuf();
        try {
            const nlohmann::json sidecar = nlohmann::json::parse(sidecarInput);
            if (!sidecar.is_object() || sidecar.value("schemaVersion", 0) != 1 || sidecar.value("runtime", "") != "lua" ||
                !sidecar.contains("behaviorTypeId") || !sidecar["behaviorTypeId"].is_string())
                return Result<std::unique_ptr<LuaBehaviorProgram>>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));
            auto typeId = BehaviorTypeId::Parse(sidecar["behaviorTypeId"].get<std::string>());
            if (typeId.HasError())
                return Result<std::unique_ptr<LuaBehaviorProgram>>::Failure(typeId.ErrorValue());
            return Compile(source.str(), std::move(typeId).Value(), sourcePath.string(), limits);
        } catch (const nlohmann::json::exception &exception) {
            return Result<std::unique_ptr<LuaBehaviorProgram>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, exception.what()));
        }
    }

    /** @copydoc LuaBehaviorProgram::Descriptor */
    const BehaviorDescriptor &LuaBehaviorProgram::Descriptor() const noexcept {
        return impl_->descriptor;
    }

    /** @copydoc LuaBehaviorProgram::Registration */
    BehaviorRegistration LuaBehaviorProgram::Registration() noexcept {
        return {impl_->descriptor, {this, &LuaBehaviorProgram::CreateInstance, &LuaBehaviorProgram::DestroyInstance}};
    }

    /** @copydoc LuaBehaviorProgram::ReplaceCompatible */
    Result<void> LuaBehaviorProgram::ReplaceCompatible(std::unique_ptr<LuaBehaviorProgram> candidate) {
        if (!candidate || !Compatible(impl_->descriptor, candidate->impl_->descriptor))
            return LuaFailure("Lua behavior reload requires a schema-compatible candidate or play-session restart.");
        impl_->descriptor.displayName = candidate->impl_->descriptor.displayName;
        impl_->descriptor.category = candidate->impl_->descriptor.category;
        impl_->source = std::move(candidate->impl_->source);
        impl_->sourceName = std::move(candidate->impl_->sourceName);
        impl_->limits = candidate->impl_->limits;
        ++impl_->revision;
        return Result<void>::Success();
    }

    /** @copydoc LuaBehaviorProgram::Revision */
    std::uint64_t LuaBehaviorProgram::Revision() const noexcept {
        return impl_->revision;
    }

    IBehaviorInstance *LuaBehaviorProgram::CreateInstance(
        void *userData) {  // NOSONAR(cpp:S5008) BehaviorFactoryBinding mandates void* userData.
        return std::make_unique<LuaBehaviorInstance>(*static_cast<LuaBehaviorProgram *>(userData)).release();
    }

    void LuaBehaviorProgram::DestroyInstance(void *, IBehaviorInstance *instance) noexcept {  // NOSONAR(cpp:S5008) Same binding contract.
        const std::unique_ptr<IBehaviorInstance> owned{instance};
    }
}  // namespace Horo::Gameplay
