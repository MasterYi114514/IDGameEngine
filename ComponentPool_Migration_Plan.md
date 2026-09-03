# 组件池化迁移计划书（ComponentPool Migration Plan）

> **文档性质**：执行计划书，供后续 AI / 开发者按步骤执行。
> **撰写时项目状态**：commit 于池化改造前（执行 Step 0 时请记录确切 commit hash）。
> **阅读约定**：本文档假设读者没有原始讨论的上下文，所有关键事实（文件路径、行号、代码行为）均自包含。

---

## 0. 给执行 AI 的强制规范（先读这里）

1. **禁止自行编译**。所有 `cmake --build` / `msbuild` / `g++` 等编译命令必须由用户在 **PowerShell** 中执行（git-bash 与用户工具链 PATH 不一致，产出的 DLL 会不匹配）。每个带 🧪 标记的"编译验收节点"，AI 必须向用户提供：**具体操作步骤 + 预期看到的现象**，然后等待用户反馈。
2. **禁止跳步**。步骤按顺序执行，每步的验收标准全部通过后才进入下一步。
3. **每步一次 git commit**（用户执行或经用户确认后执行），保证可单步回退。
4. 改动前先读该步"前置阅读"列出的文件与行号，禁止凭本文档的描述直接改代码——本文档记录的是设计意图，代码以实际内容为准。
5. 遵循项目 skill：`.pi/skills/id-coding-style/`（命名空间 `ID`、include 路径以 `include/`、`src/` 为根、`#ifdef` 包裹整块等）。
6. 用户只说"继续下一步"时，也要先重读本步前置阅读的文件确认代码未被中途修改。

---

## 1. 背景与目标

### 1.1 现状（池化前的关键事实）

当前 Scene 模块是 OOP 式 GameObject-Component 模式：

| 事实 | 位置 | 说明 |
|------|------|------|
| 组件堆分配离散存储 | `ID/include/Scene/GameObject.hpp` | `std::vector<std::unique_ptr<Component>> m_components` + `unordered_map<TypeID, Component*> m_component_index` + 同类型链表（`Component::m_next`） |
| 组件类型共 6 种 | `ID/include/Scene/Component/Component.hpp` | Transform / MeshRenderer / Light / RigidBody / AudioSource / AudioListener，TypeID 由 consteval 类型列表在**编译期**确定（跨 DLL 稳定，**必须保留此机制**） |
| `remove_component` 不调 `on_detach` | `ID/include/Scene/GameObject.hpp` `remove_component()` | 只做出链 + erase，走析构。而 `~GameObject()`（`ID/src/Scene/GameObject.cpp`）显式遍历调用 `on_detach()`。**这是已知隐患，本次顺带修复** |
| PhysicsSystem 每帧 5 次全场景扫描 | `ID/src/Scene/System/PhysicsSystem.cpp:95,117,143,220,253` | 每次调用 `find_game_objects_with_component<RigidBodyComponent>()`，O(所有 GO) + 堆分配 |
| Renderer 场景收集同样全扫描 | `ID/src/Renderer/Render/Renderer.cpp:421-470` | MeshRenderer / Light 两段循环 |
| 组件与外部系统全部以句柄关联 | 各 Component | `RigidBodyID` / `AudioSourceID`，无组件指针外泄 |
| 全项目无跨帧组件指针缓存 | grep 验证过 | 所有 `get_component<T>()` 均为即时使用 |
| GameObject 地址稳定 | `ID/include/Scene/Scene.hpp` | `vector<unique_ptr<GameObject>>` 存储，`Component::m_owner` 指针安全 |
| 反序列化经 ComponentFactory | `ID/src/Scene/GameObject.cpp` `deserialize()` | `create(type_name)` 返回 `unique_ptr<Component>` → `deserialize(json)` → `on_attach(this)` |
| Scene 析构顺序教训 | `ID/src/Scene/Scene.cpp` `~Scene()` 注释 | 组件 `on_detach` 访问 `PhysicsWorld`，必须在 PhysicsSystem 析构前释放组件（use-after-free 教训） |
| CMake 自动收集源文件 | `ID/CMakeLists.txt` | `file(GLOB_RECURSE ...)`，新增 `.hpp/.cpp` **无需修改构建脚本** |

### 1.2 目标

1. **组件按类型池化连续存储**：每类型一个 `ComponentPool<T>`（sparse set：dense 组件数组 + GO ID → dense 下标的稀疏映射）。
2. **GameObject 改持句柄**：删除 `m_components` / `m_component_index` / `m_next` 链表；`add/get/has/remove_component<T>()` 四个模板函数改走 Scene 持有的 `ComponentRegistry`。
3. **热路径按池遍历**：PhysicsSystem（5 处）、Renderer 收集（2 处）、AudioSystem 更新、AssetPanel（1 处）从"全场景扫描"改为直接遍历池。
4. **调用方零改动**：`get_component<T>()` 签名与返回语义（`T*`，含 nullptr 语义）完全不变；InspectorPanel 等 DevGUI 代码不修改。

### 1.3 非目标（本计划明确不做）

- 不做组件 POD 化，虚函数（`on_attach/on_detach/on_update/on_event/serialize` 等）全部保留。
- 不把组件行为搬进 System（AudioSource 的 `on_update` 仍以虚函数形式被调用，只是调用方从 GameObject 改为按池遍历）。
- 不改场景 JSON 格式（components 数组元素内容不变；**数组顺序会变**，但按 `type` 字段反序列化，新旧存档双向兼容）。
- 不动 GameObject 的父子层级、name、active 语义。

### 1.4 DevGUI 影响评估（已逐点验证，结论：不会失效）

池化对 DevGUI 的影响经过代码级验证，现有 DevGUI 的访问模式恰好是池化最安全的模式：

| # | 验证过的 DevGUI 事实 | 位置 | 池化兼容性 |
|---|---------------------|------|-----------|
| A | InspectorPanel 每帧开头做 `is_game_object_valid` 检查，然后按 ID 取 GO、即时 `get_component`，引用仅在当帧绘制内使用 | `InspectorPanel.cpp` `on_imgui_render()` | ✅ ImGui 立即模式：DragFloat 虽跨帧交互，但每帧重新 get，地址自动刷新 |
| B | Panel 之间只传 `GameObject::ID`（`set_context(scene, id)` / `set_selected_object`），从不传递组件指针 | `InspectorPanel.hpp` / `SceneHierarchyPanel.hpp` | ✅ 与池搬移解耦 |
| C | `remove_component` 全项目**零调用**——DevGUI 没有 Remove Component 入口 | grep 验证 | ✅ 不存在"移除组件操作失效"问题 |
| D | Add Component 菜单：`has_component` 置灰 + `add_component<T>().make_active()` 即用即弃；**AudioSource 项当前永远可用（注释"允许多个"）** | `InspectorPanel.cpp` `render_add_component_menu()` | ⚠️ 单实例退化后需微调：置灰逻辑改为与其他组件一致，否则二次点击静默返回已有组件，用户无反馈 |
| E | 拖拽 Position 的副作用链：`set_position` → 脏传播（经 GO ID 取子级 Transform）→ 下一帧 PhysicsSystem 同步（经 ID/池） | `TransformComponent.cpp` | ✅ 全链经 ID/句柄，无指针链 |
| F | Duplicate = `serialize` → `clone_tree` 反序列化 | `SceneHierarchyPanel.cpp` | ✅ 走 Step 2 改造的序列化路径，需验收覆盖 |
| G | AssetPanel 遍历 `find_game_objects_with_component<MeshRendererComponent>()` | `AssetPanel.cpp:355` | ✅ Step 5 改池遍历，返回 ID 列表语义不变 |
| H | 同一 ImGui 帧内 "Hierarchy 删除 GO" 与 "Inspector 绘制" 并存 | — | ✅ 两种绘制顺序均安全（先删后绘：Inspector 拿新地址；先绘后删：引用已用完，下一帧走有效性检查） |

**必须写入 DevGUI 代码注释的两条规矩**（Step 6 固化）：
1. Panel 内禁止跨帧缓存组件引用/指针，只允许缓存 `GameObject::ID`；
2. Panel 之间只传 ID，不传组件引用。

现有代码已满足这两条，立规矩的目的是防止未来新增面板时踩坑（坑 C 的 DevGUI 版本）。

---

## 2. 总体设计

### 2.1 存储结构

```
Scene
 ├─ m_game_objects: vector<unique_ptr<GameObject>>     （不变，entity 容器）
 ├─ m_component_registry: ComponentRegistry             （新增）
 │    ├─ m_pools: IComponentPool* [6]                   （按现有 consteval TypeID 索引）
 │    │    ├─ ComponentPool<TransformComponent>
 │    │    ├─ ComponentPool<MeshRendererComponent>
 │    │    ├─ ComponentPool<LightComponent>
 │    │    ├─ ComponentPool<RigidBodyComponent>
 │    │    ├─ ComponentPool<AudioSourceComponent>
 │    │    └─ ComponentPool<AudioListenerComponent>
 │    └─ 模板 pool<T>() / 类型擦除 for_each / erase_all_of(owner)
 └─ m_physics_system                                    （不变）
```

```cpp
// ComponentPool<T> 内部（示意）
template<typename T>
class ComponentPool : public IComponentPool
{
    std::vector<T>              m_components;  // dense：连续存储，缓存友好
    std::vector<GameObject::ID> m_owners;      // dense[i] 的归属 GO（与 m_components 同下标）
    std::vector<uint32_t>       m_sparse;      // sparse[go_id] → dense 下标，NULL_INDEX 表示无
};
```

- `m_sparse` 按 `GameObject::ID` 直接下标访问，容量随 `create_game_object` 增长（`resize` 填 `NULL_INDEX`）。
- **删除采用 swap-and-pop**：先对被删元素调 `on_detach()`，再用尾部元素 move 覆盖洞位，并更新被移动元素的 `m_sparse` 条目。**顺序不可颠倒**（详见 2.3 坑 A）。
- `s_allow_multiple = false` 的单实例约束由 sparse set 天然满足（每 GO 每类型至多一个槽位）。
- **`AudioSourceComponent`（原 `s_allow_multiple = true`）已决策：接受退化为单实例**（用户于计划评审时确认）。配套 UI 适配见 1.4 节 D 项与 Step 2。

### 2.2 接口设计（新增两个头文件）

**`ID/include/Scene/Component/ComponentPool.hpp`**（纯模板，无 DLL 导出问题）：

```cpp
namespace ID
{
    class IComponentPool            // 类型擦除基类，供 Registry 统一持有
    {
    public:
        virtual ~IComponentPool() = default;
        virtual bool  has(GameObject::ID owner) const = 0;
        virtual void  erase(GameObject::ID owner) = 0;          // on_detach + swap-pop
        virtual size_t size() const = 0;
        virtual void   reserve_for_game_objects(size_t capacity) = 0;  // sparse 数组扩容
        // serialize 支持见 Step 2 实现要点 5
    };

    template<typename T>            // T 必须 is_base_of_v<Component, T>
    class ComponentPool : public IComponentPool { ... };
}
```

**`ID/include/Scene/Component/ComponentRegistry.hpp`**：

```cpp
namespace ID
{
    class ComponentRegistry
    {
    public:
        template<typename T> ComponentPool<T>&  pool();
        template<typename T> T*                 get(GameObject::ID owner);      // 不存在返回 nullptr
        template<typename T> bool               has(GameObject::ID owner) const;
        template<typename T> T&                 emplace(GameObject::ID owner);  // 前置：!has(owner)
        template<typename T> void               erase(GameObject::ID owner);

        void    reserve_for_game_objects(size_t capacity);   // 转发给所有池
        void    erase_all_of(GameObject::ID owner);          // GO 销毁时逐池清理
        // 类型擦除遍历（serialize / deserialize / on_update 用）：
        // for_each(owner, fn(type_id, IComponentPool&))
    };
}
```

> 命名注意：`ID/src/BasicPool.hpp` 是已存在的"槽位复用型"池，语义不同，**不要复用也不要改名它**，本次新增的类严格使用 `ComponentPool` 名字。

### 2.3 本方案已识别的坑（执行中随时回来对照）

**坑 A —— erase 清理顺序**：`on_detach()`（释放 `RigidBodyID` / `AudioSourceID`）必须在 move 覆盖之前显式调用，否则句柄已被 move 走，清理逻辑拿不到句柄，物理/音频世界残留孤儿对象。

**坑 B —— 迭代中结构性修改**：按池遍历期间 `emplace/erase` 会搬动 dense 数组使迭代失效。现有代码（Audio 组件 `on_update` 内只读 Transform）不触发，但必须在池迭代器 / 遍历辅助函数处加 Debug 断言（`ID_ASSERT` 或日志），并写入头文件注释立规矩。

**坑 C —— `T&`/`T*` 失效窗口**：组件引用禁止跨帧缓存、禁止跨越任何 `add/remove_component` 调用持有。写入 GameObject.hpp 注释。

**坑 D —— Scene 析构顺序**：延续 `~Scene()` 现有模式——**显式**先 `erase_all_of`（此时 PhysicsSystem 仍存活，`on_detach` 能安全访问 PhysicsWorld），再清 `m_game_objects`，最后清池。禁止依赖成员声明顺序的隐式析构。

**坑 E —— on_update / on_event 传播顺序变化**：从"按 GO、按添加顺序"变为"按池、按 dense 下标"。现有组件无顺序依赖（`RigidBodyComponent::on_update` 为空，Audio 只读 Transform），预期无行为变化，但 Step 5 验收要覆盖音频跟随场景。

---

## 3. 分步执行计划

---

### Step 0：基线快照（无代码改动）

**目标**：固化"改造前正确行为"的对照基线；建立工作分支。

**操作**：
1. 请用户在 PowerShell 执行 `git status` 确认工作区干净，然后 `git checkout -b feature/component-pool`。
2. 请用户启动 Sandbox（`bin/` 下，按用户日常方式运行），AI 引导用户确认并记录以下基线现象（拍照或文字记录均可）：
   - 场景加载后：模型正常显示、光照正常、阴影正常。
   - 运行（Play）后：带 RigidBody 的立方体自由落体、落地静止。
   - Inspector 面板：能看到 Transform / MeshRenderer 等组件并可编辑（拖动位移滑条，视口中物体移动）。
   - Scene Hierarchy 面板：Add Component 菜单可用；删除一个带 RigidBody 的 GameObject 不崩溃。
   - 保存场景后重新加载，场景内容完整。

**验收标准**：基线现象记录完毕（作为后续每步的对照），分支已建立。

**回退**：无代码改动，无需回退。

---

### Step 1：新增 ComponentPool（纯新增，不接线）

**目标**：实现 `ComponentPool<T>` 与 `IComponentPool`，不接入任何现有代码路径。

**前置阅读**：
- `ID/include/Scene/Component/Component.hpp`（TypeID 机制、`on_detach` 语义）
- `ID/include/Scene/GameObject.hpp`（`GameObject::ID` 定义、现有 `add/remove_component` 行为——你要在 Step 2 复刻的语义）
- `ID/src/BasicPool.hpp`（确认命名区分，不要动它）

**改动清单**：
| 文件 | 操作 |
|------|------|
| `ID/include/Scene/Component/ComponentPool.hpp` | 新增 |

**实现要点**：
1. `emplace(owner, args...)`：`m_sparse` 不足时 resize 填 `NULL_INDEX`；dense 尾部构造；登记 `m_owners` / `m_sparse`。返回 `T&`。
2. `find(owner)` / `has(owner)`：查 `m_sparse`。
3. `erase(owner)`：**① 对被删元素调 `on_detach()` → ② 尾元素 move 赋值覆盖洞位 → ③ 更新被移动元素的 `m_sparse[其owner]` → ④ `pop_back`（components 与 owners 两个数组）→ ⑤ `m_sparse[owner] = NULL_INDEX`**。唯一元素时跳过 ②③。不存在的 owner 直接返回。
4. 遍历接口：提供 `components()`（`std::span<T>`）与 `owners()`（`std::span<const GameObject::ID>`）即可，暂不做花哨迭代器。
5. Debug 防护：加一个 `m_iterating` 标志与 `begin_iteration()/end_iteration()` RAII 辅助（可选，Step 5 用），`emplace/erase` 在 `m_iterating` 时记 `ID_ERROR` 日志（坑 B）。
6. 全部实现放头文件（模板豁免于"三行规则"）；非模板的 `IComponentPool` 保持纯虚接口，无需 .cpp。
7. include 路径遵守项目规范：`#include "Scene/GameObject.hpp"` 仅取 `GameObject::ID`——注意 GameObject.hpp 较重，若只想用 ID 可自行定义 `using EntityID = uint32_t;` 别名以解耦（推荐，注释说明与 `GameObject::ID` 同型）。

**验收标准（静态）**：
- 新文件符合项目风格（命名空间 `ID`、`#pragma once`、include 以 `include/` 根路径书写）。
- 未修改任何既有文件（`git diff` 除新文件外为空）。

**🧪 编译验收节点 1**：
- AI 向用户提供如下指引（示例，按用户实际构建习惯调整）：
  > "请主人在 PowerShell 中执行你常用的构建命令（如 `cmake --build build --config Debug`），新增文件会被 CMake 的 GLOB_RECURSE 自动收集，无需改 CMakeLists。"
- **预期现象**：编译成功，无 error；警告与基线一致（新文件自身不产生警告）。
- Sandbox 运行行为与 Step 0 基线**完全一致**（此步没有接线，理论上零影响；若行为变化说明误改了既有文件，`git diff` 排查）。

**回退**：删除新文件即可。

---

### Step 2：Registry 接入 + GameObject 四入口切换（核心手术）

**目标**：组件存储切换到池；删除 GameObject 内的旧容器；序列化/反序列化路径改造。

**前置阅读**：
- `ID/include/Scene/GameObject.hpp`（全文——四个模板函数的现有语义要逐一复刻）
- `ID/src/Scene/GameObject.cpp`（`~GameObject`、`on_update/on_event`、`serialize/deserialize`）
- `ID/src/Scene/Scene.cpp`（`create/destroy_game_object`、`~Scene` 析构顺序注释、`find_game_objects_with_component`）
- `ID/include/Scene/Component/ComponentFactory.hpp`（Creator 签名）
- `ID/src/Scene/Component/AudioSourceComponent.cpp`（`on_attach/on_detach` 的资源语义）

**改动清单**：
| 文件 | 操作 |
|------|------|
| `ID/include/Scene/Component/ComponentRegistry.hpp` | 新增 |
| `ID/include/Scene/Component/ComponentPool.hpp` | 按需补充 serialize 类型擦除接口 |
| `ID/include/Scene/Scene.hpp` | 成员 `m_component_registry`；`destroy_game_object` 内先 `erase_all_of`；保留 `find_game_objects_with_component`（Step 5 才删） |
| `ID/src/Scene/Scene.cpp` | `~Scene` 显式清理顺序（坑 D）；`create_game_object` 时 `reserve_for_game_objects` |
| `ID/include/Scene/GameObject.hpp` | 四个模板函数改走 `m_scene->get_component_registry()`；删 `m_components`/`m_component_index`；删 `~GameObject` 中组件遍历 |
| `ID/src/Scene/GameObject.cpp` | `serialize` 改为经 Registry 遍历该 GO 的组件；`deserialize` 改造 |
| `ID/include/Scene/Component/ComponentFactory.hpp` | `ComponentCreator` 签名改为 `std::function<bool(GameObject&, const Json&)>`（内部完成 add + deserialize），`ID_REGISTER_COMPONENT` 宏同步修改 |
| `ID/src/Scene/Component/ComponentFactory.cpp` | `create` 相应调整 |

**实现要点**：
1. **`add_component<T>` 语义复刻清单**（对照现有实现逐条保留）：
   - 单实例检查：池的 `has()` 即检查（等价于现有 `m_component_index.find`）。
   - 前置依赖自动补 Transform：现有 4 段 `if constexpr` 原样保留（后续可用 `requires` 收敛，本步不做）。
   - 顺序：先构造进池 → 再 `on_attach(this)` → 返回引用（与现状 push 后 attach 一致）。
2. **`remove_component<T>` 行为修复（主动变更，需在 commit message 注明）**：改为池 `erase`（内含 `on_detach`），修复"移除 RigidBody 组件不释放物理刚体"的历史隐患。
3. **`~GameObject` / `destroy_game_object` / `~Scene` 职责重划**：
   - GO 不再拥有组件 → `~GameObject` 删除组件遍历。
   - `Scene::destroy_game_object`：销毁 GO 前 `m_component_registry.erase_all_of(id)`（逐池 erase，触发各组件 `on_detach`）。
   - `~Scene()`：`① 对每个存活 GO 逐池 erase_all_of → ② m_game_objects.clear() → ③ registry 清空`。此时物理系统仍存活，`on_detach` 安全（坑 D）。
4. **`get_component<T>` 保持 `T*` 返回**，找不到返回 `nullptr`——调用方（PhysicsSystem/Renderer/DevGUI 共 30+ 处）零改动。
5. **serialize**：Registry 提供 `for_each_component_of(GameObject::ID, fn)`，fn 拿到 `Component&`（池内元素的基类引用）调 `serialize(arena)`。组件在 JSON 数组中的顺序变为按池固定顺序，格式兼容（1.3 节）。
6. **deserialize**：`ComponentFactory::create` 新签名直接接收 `(GameObject&, const Json&)`，内部 `go.add_component<T>()` + `->deserialize(json)`。Creator lambda 里 T 是具体类型，天然知道进哪个池。
7. **Component 基类**：本步**暂不删** `m_next/get_next/set_next`（Step 6 清理），但 GameObject 侧不再使用。新增 `static_assert(is_base_of_v<Component, T>)` 于池模板。
8. **AudioSource 单实例 UI 适配（R2 已决策）**：`InspectorPanel.cpp` `render_add_component_menu()` 中 AudioSource 的 `MenuItem` 从"永远可用"改为与其他组件一致的 `has_component` 置灰逻辑，并同步修改注释（原注释"允许多个"已过时）。

**验收标准（静态）**：
- `grep -rn "m_component_index\|m_components" ID/` 在 Scene 模块内无残留（GameObject.hpp/cpp 之外本就没有）。
- `grep -rn "get_next()\|set_next(" ID/` 仅剩 Component.hpp 定义处（Step 6 删）。
- InspectorPanel / SceneHierarchyPanel / Renderer / PhysicsSystem 的 .cpp **一行未改**（本步刻意不动调用方）。

**🧪 编译验收节点 2**：
- AI 指引："请主人在 PowerShell 中执行常用构建命令。"
- **预期现象**：
  - 编译成功。可能出现 `ComponentFactory` 相关的未使用警告（`is_registered` 等旧接口），记录但不必清（Step 6 处理）。
  - Sandbox 启动后**所有 Step 0 基线现象逐项一致**：场景加载渲染正常、Play 物理正常、Inspector 各组件可见可编辑、Add Component 可用、删除带 RigidBody 的 GO 无崩溃且掉落物体消失、场景保存/重载完整。
  - **Duplicate 验证**：在 Hierarchy 右键 Duplicate 一个带 MeshRenderer + RigidBody 的 GO，副本渲染与物理行为正常（走新序列化路径 F 项）。
  - **Add Component 菜单验证**：AudioSource 添加一次后菜单项变灰（R2 适配生效）；其余组件置灰行为与基线一致。
  - 额外验证：删除 GO 后再新建同名 GO（触发 ID 复用），Inspector 与渲染均正常——这验证 `m_sparse` 槽位复用正确。
  - 额外验证：对一个 GO 手动 remove MeshRenderer 组件（若 DevGUI 有此入口；没有则跳过），视口中物体消失。
- **异常排查**：崩溃在 `on_detach` → 检查 erase 顺序（坑 A）；保存场景组件丢失 → 检查 `for_each_component_of`；加载场景缺 Transform → 检查 deserialize 的 add 路径。

**回退**：`git revert` 本步 commit（Step 1 的池文件与本步解耦，可保留）。

---

### Step 3：PhysicsSystem 热路径改池遍历

**目标**：消除每帧 5 次 `find_game_objects_with_component<RigidBodyComponent>()` 全场景扫描。

**前置阅读**：
- `ID/src/Scene/System/PhysicsSystem.cpp` **95~279 行**（5 处调用各自的上下文：`sync_rigid_bodies` / `push_transforms` / `pull_transforms` / `sync_pose` 周边逻辑，特别是"GO 已销毁/组件已移除"的检测方式）
- `ID/include/Scene/System/PhysicsSystem.hpp`

**改动清单**：
| 文件 | 操作 |
|------|------|
| `ID/src/Scene/System/PhysicsSystem.cpp` | 5 处循环改为遍历 `RigidBodyComponent` 池 |
| `ID/include/Scene/Scene.hpp` | 视情况新增 `get_component_registry()` 访问器（若 Step 2 已加则跳过） |

**实现要点**：
1. 改造模式统一为：
   ```cpp
   auto& pool = m_scene->get_component_registry().pool<RigidBodyComponent>();
   for (size_t i = 0; i < pool.size(); ++i)
   {
       GameObject::ID owner = pool.owners()[i];
       if (!m_scene->is_game_object_valid(owner)) continue;   // 防御，正常不触发
       RigidBodyComponent& comp = pool.components()[i];
       TransformComponent* transform = m_scene->get_component_registry().get<TransformComponent>(owner);
       ...
   }
   ```
2. **下标循环而非 range-for**：池 erase 会搬动数组；本函数内不增删组件，用下标最直观。同步注意 `sync_rigid_bodies` 里若有"移除失效刚体"的分支，改为收集后延迟 erase（坑 B），或确认其本就只标记不删除。
3. `sync_pose(RigidBodyID)` 单个刚体的同步保持 `get<RigidBodyComponent>(owner)` 查询式即可。
4. 语义保持：遍历顺序变化（按池 dense 顺序）对物理无影响（各刚体独立同步），但 `pull_transforms` 若存在"先父后子"假设需核实——从现有代码看刚体同步不依赖 GO 层级顺序，Transform 世界矩阵缓存由 `TransformComponent` 自身脏标记保证。

**验收标准（静态）**：`grep -n "find_game_objects_with_component" ID/src/Scene/System/PhysicsSystem.cpp` 零匹配。

**🧪 编译验收节点 3**：
- AI 指引："请主人在 PowerShell 中执行常用构建命令，然后运行 Sandbox。"
- **预期现象**：
  - Play 后立方体自由落体、落地弹跳/静止与基线一致（重力、碰撞 restitution）。
  - Play 中在 Inspector 修改 mass / damping，行为即时变化（验证 setter → `m_need_sync` → 池内组件路径完好）。
  - Trigger 开关行为不变（Static + mass=0 切换）。
  - 删除正在模拟的动态刚体 GO：无崩溃，物理世界无残留（连续删除 3~5 个）。
  - 可选：StatsPanel 若显示帧时间，对比基线应有持平或小幅下降（收益主要在 Step 4/5 后才明显）。
- **异常排查**：物体穿地/不动 → `pull/push_transforms` 适配错误；删除后崩溃 → erase 搬动与遍历并发（坑 B），检查延迟 erase。

**回退**：revert 本步 commit。

---

### Step 4：Renderer 场景收集改池遍历

**目标**：`Renderer::render()` 的 MeshRenderer / Light 收集从全扫描改池遍历。

**前置阅读**：
- `ID/src/Renderer/Render/Renderer.cpp` 415~475 行（两段收集循环的完整上下文：`is_active` 双重检查、缺 Transform 的警告分支）

**改动清单**：
| 文件 | 操作 |
|------|------|
| `ID/src/Renderer/Render/Renderer.cpp` | 两段循环改池遍历 |

**实现要点**：
1. 保留语义：GO inactive 跳过、组件 inactive 跳过、缺 TransformComponent 记警告并跳过、`Light.enabled` 检查。
2. 手动 submit 路径（非场景模式）不动。
3. 排序逻辑（② 相机距离排序）不动——收集顺序变化不影响结果（排序在收集后统一进行）。

**验收标准（静态）**：`grep -n "find_game_objects_with_component" ID/src/Renderer/Render/Renderer.cpp` 零匹配。

**🧪 编译验收节点 4**：
- AI 指引："请主人在 PowerShell 中执行常用构建命令，然后运行 Sandbox。"
- **预期现象**：
  - 画面与 Step 0 基线**逐像素一致**（模型、光照、阴影、天空盒）。
  - 在 Hierarchy 中隐藏某个 GO（set_active false）：视口中对应物体消失。
  - 透明物体（若基线场景有）渲染顺序正常（back-to-front 不受收集顺序影响）。
- **异常排查**：个别物体消失 → 检查 owner 的 GO active 检查遗漏；灯光全灭 → Light 池遍历的 `enabled` 分支。

**回退**：revert 本步 commit。

---

### Step 5：组件 on_update / on_event 按池遍历 + 收尾热路径

**目标**：`Scene::on_update/on_event` 从"遍历 GO → GO 遍历组件"改为"按池遍历"；清理 AssetPanel 最后一处全扫描。

**前置阅读**：
- `ID/src/Scene/GameObject.cpp`（`on_update/on_event`——要搬走的逻辑）
- `ID/src/Scene/Scene.cpp`（`on_update/on_event`）
- `ID/src/DevGUI/ImGui/Panels/AssetPanel.cpp` 350~365 行
- `ID/src/Scene/Component/AudioSourceComponent.cpp` / `AudioListenerComponent.cpp` 的 `on_update`（确认池遍历下语义不变：只读自己与 Transform）

**改动清单**：
| 文件 | 操作 |
|------|------|
| `ID/src/Scene/Scene.cpp` | `on_update`：物理系统后，逐池遍历调用 `on_update`；`on_event` 同理（保留 `is_handled` 短路） |
| `ID/include/Scene/GameObject.hpp` / `ID/src/Scene/GameObject.cpp` | 删除 `on_update/on_event` 中的组件遍历（GO 层不再驱动组件；函数可保留为空壳或删除，与调用方确认） |
| `ID/src/DevGUI/ImGui/Panels/AssetPanel.cpp` | `find_game_objects_with_component<MeshRendererComponent>` 改池遍历 |

**实现要点**：
1. 池遍历时用 Step 1 的 `m_iterating` RAII 防护（坑 B 断言在此生效）。
2. GO active / 组件 active 双重检查保留：`!go.is_active() → skip`；`!comp.is_active() → skip`。
3. `on_event` 的 `is_handled()` 短路：跨池顺序为 Registry 池声明顺序（Transform → MeshRenderer → Light → RigidBody → AudioSource → AudioListener）。现有组件中仅 Audio 有 `on_event` 语义需确认（执行时读实现）。
4. 坑 E 在本步生效：验收必须覆盖音频跟随。

**验收标准（静态）**：`grep -rn "find_game_objects_with_component" ID/` 全项目零匹配（函数本体此时也可删除）。

**🧪 编译验收节点 5（最终运行验收）**：
- AI 指引："请主人在 PowerShell 中执行常用构建命令，然后运行 Sandbox 做全功能冒烟。"
- **预期现象**：
  - **音频**：带 AudioSource 的物体播放正常；物体移动时听感/衰减跟随（3D 空间化）；AudioListener 挂载物体移动时听感正确。
  - 全量复跑 Step 0 基线清单 + Step 2~4 的所有额外验证项。
  - 压力验证：场景中放置 20+ 个带 MeshRenderer 的 GO，Play 帧率不低于基线（预期持平或更好）。

**回退**：revert 本步 commit。

---

### Step 6：清理与文档固化

**目标**：删除死代码，固化文档。

**改动清单**：
| 文件 | 操作 |
|------|------|
| `ID/include/Scene/Component/Component.hpp` | 删 `m_next/get_next/set_next/is_tail`；类注释更新（组件由 Scene 的 ComponentRegistry 池持有） |
| `ID/include/Scene/GameObject.hpp` | 类注释写明坑 C 规矩（组件引用禁止跨帧/跨结构性操作缓存）；删除 `default_game_object`（若确认无引用） |
| `ID/include/Scene/Scene.hpp` | 删 `find_game_objects_with_component`（Step 5 已无调用） |
| `docs/ComponentPool_Migration_Plan.md` | 末尾追加"实施记录"小节：各步实际 commit hash、偏离计划的改动及原因 |
| `.pi/agent/AGENTS.md` 或架构笔记 | 补一行：Scene 模块已池化，后续 ECS 演进（行为入 System / 组件 POD 化）以 ComponentRegistry 为存储基座 |

**验收标准（静态）**：
- `grep -rn "get_next()\|set_next(\|m_component_index\|find_game_objects_with_component" ID/` 全零匹配。
- 全量编译零警告（或警告数不多于基线）。

**🧪 编译验收节点 6**：最终构建 + Step 0 基线全量复跑，全部一致则迁移完成。

---

## 4. 总验收清单（迁移完成的定义）

| # | 项目 | 通过标准 |
|---|------|---------|
| 1 | 编译 | PowerShell 构建成功，无新增警告 |
| 2 | 渲染 | 画面与基线一致；隐藏/删除 GO 行为正确 |
| 3 | 物理 | 落体/碰撞/Trigger/参数热改与基线一致；删除动态刚体无残留无崩溃 |
| 4 | 音频 | 播放/3D 跟随正常 |
| 5 | 序列化 | 旧存档可读；新存档重读完整；Duplicate 副本功能正常 |
| 6 | DevGUI | Inspector/Hierarchy/AssetPanel 功能正常；Add Component 置灰逻辑正确（含 AudioSource 单实例） |
| 7 | ID 复用 | 删 GO 后新建 GO（复用 ID）各项功能正常 |
| 8 | 死代码 | 第 6 节 grep 全零 |
| 9 | 性能 | 20+ 物体场景帧率 ≥ 基线 |

## 5. 风险登记表

| 编号 | 风险 | 影响步骤 | 缓解 |
|------|------|---------|------|
| R1 | erase 时 move 覆盖先于 on_detach，句柄丢失 | Step 1/2 | 坑 A 的固定顺序 + 单元验证（删带 RigidBody 的 GO） |
| R2 | AudioSourceComponent 多实例退化 | Step 2 | **已决策：接受单实例**。UI 置灰适配 + 头文件 `s_allow_multiple` 注释标记"暂缓，池化限制"；未来需要时再扩展池多槽支持 |
| R3 | 迭代中结构性修改致迭代器失效 | Step 3/5 | `m_iterating` 断言 + 延迟 erase |
| R4 | Scene 析构 UAF 复发 | Step 2 | 坑 D 显式清理顺序，注释写明教训 |
| R5 | JSON 组件顺序变化导致某处隐式依赖 | Step 2 | 反序列化按 type 分发，理论无影响；验收 5 覆盖 |
| R6 | 跨 DLL TypeID 退化 | 全局 | 不引入任何运行时类型计数器；继续用现有 consteval 列表做池索引 |
