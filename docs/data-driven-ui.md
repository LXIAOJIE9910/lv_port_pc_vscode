# 数据驱动 UI 架构 — JSON + C 行为

> 基于 lv_port_pc_vscode, 在 app_launcher 上实现的量产级数据驱动 UI 方案。
> 解决"量产 AI 眼镜 UI 要灵活调整、用户自定义、不重编译"的诉求。

---

## 一、设计思想

把"UI 长什么样"从代码抽出来变数据,代码只读数据生成 UI。

```
JSON (布局+样式+事件绑定)    ← 热改, 改了不编译
   ↓ ui_engine_read_file + mj_parse
通用 UI 引擎 (C, 固件里)      ← 编译一次, 通用
   ↓ create_one_control 分发
LVGL 控件树
   ↓ on_change 查动作表
C 动作库 (业务逻辑)            ← 编译, 高效可调试
```

**热改的部分**: 布局、样式、事件绑定、文本、app 列表
**编译的部分**: 业务逻辑(动作函数)、UI 引擎本身

---

## 二、文件结构

### 新增
| 文件 | 作用 |
|------|------|
| `src/cjson/mini_json.h/.c` | 手写最小 JSON 解析器(对象/数组/字符串/数字/布尔/null), 用 lv_malloc 接 LVGL 内存管理 |
| `src/ui_engine.h/.c` | 通用 UI 引擎: 读文件 + 解析 JSON + 建 LVGL 控件 + 颜色/symbol 解析 + 事件绑定 |
| `src/ui_actions.h/.c` | C 动作库: set_brightness/toggle_bluetooth 等, 编译进固件但绑定由 JSON 决定 |
| `apps.json` | home 页 app 列表(热改) |
| `settings_ui.json` | Settings app 内部界面(热改) |

### 改造
| 文件 | 改动 |
|------|------|
| `src/app_launcher.c` | 重写: 读 apps.json → 循环生成图标 → 按 type 分发(builtin/json), 保留多屏懒加载 |
| `CMakeLists.txt:75` | 加 4 个新源文件到 MAIN_SOURCES |

---

## 三、JSON Schema

### apps.json (home 页 app 列表)
```json
{
  "home": { "title": "Apps", "bg_color": "#1a1a2e", "bg_grad_color": "#16213e" },
  "apps": [
    { "name": "Music", "symbol": "AUDIO", "color": "#6f8af6", "type": "builtin", "entry": "music" },
    { "name": "Settings", "symbol": "SETTINGS", "color": "#4cd964", "type": "json", "ui_file": "A:settings_ui.json" }
  ]
}
```
- `type: "builtin"` → C 代码 app(music/widgets), `entry` 指定调哪个 C 入口
- `type: "json"` → 界面也由 JSON 描述, `ui_file` 指向 app 内部 UI 的 JSON

### settings_ui.json (app 内部界面)
```json
{
  "title": "Settings", "bg_color": "#1a1a2e",
  "controls": [
    { "id": "lbl_brightness", "type": "label", "text": "Brightness", "x": 20, "y": 60 },
    { "id": "slider_brightness", "type": "slider", "x": 20, "y": 90, "w": 200,
      "range_min": 0, "range_max": 100, "value": 70, "on_change": "set_brightness" },
    { "id": "sw_bluetooth", "type": "switch", "x": 20, "y": 140, "on_change": "toggle_bluetooth" }
  ]
}
```
`on_change` = 事件绑定, 值为动作名, C 侧 ui_actions 查表执行。

---

## 四、支持的控件与属性 (ui_engine)

| type | 属性 |
|------|------|
| label | text, color, x, y |
| button | text, color, x, y, w, on_change(CLICKED) |
| slider | range_min/max, value, x, y, w, on_change(VALUE_CHANGED) |
| switch | checked, x, y, on_change(VALUE_CHANGED) |
| image | symbol, x, y |

symbol 名映射: AUDIO/LIST/SETTINGS/HOME/WIFI/BLUETOOTH/BELL/PLAY/PAUSE 等 → LV_SYMBOL_*。

---

## 五、关键机制

### 文件读取 (LVGL 文件系统)
`LV_USE_FS_STDIO=1`, 盘符 'A', `lv_fs_open("A:apps.json", RD)` → 内部 `fopen("apps.json")` 读项目根目录。
- `ui_engine_read_file(path)`: lv_fs_path_get_size → malloc → lv_fs_open/read/close → null 终止
- 失败优雅降级(打日志 + 空 home), 不崩

### 事件绑定 (行为数据驱动)
JSON `"on_change": "set_brightness"` → ui_engine 查 `ui_actions_find("set_brightness")` → 挂 `lv_obj_add_event_cb`。
- slider/switch → LV_EVENT_VALUE_CHANGED
- button → LV_EVENT_CLICKED
- 加新动作: ui_actions.c 加函数+表, JSON 引用, 不改引擎

### 内存管理
- mini_json 用 lv_malloc/lv_free → stress 的 LV_USE_ASSERT_MEM_INTEGRITY 覆盖 JSON 解析
- 解析树(apps.json)保持存活供图标回调查 app 元数据
- app 内部 JSON(settings_ui.json)解析后立即 mj_free(控件树已建好)

### 多屏懒加载
- home_screen / music_screen / widgets_screen + json app 缓存(最多 16 个, JSON_APP_MAX)
- 每个 app 首次进入建 UI, 之后只切屏, 状态保留
- **缓存键 = app index**(在 apps.json 中的位置), 不是 ui_file 路径 → 不同 app 即使共用同一 JSON 也各自独立 screen

### 踩坑记录

**坑1: app 状态联动(缓存键选错)**
- 现象: Settings 和 WiFi 共用 settings_ui.json 时, 在 Settings 改 slider, WiFi 里也变
- 原因: 缓存按 ui_file 路径去重 → 两个 app 共享同一个 screen 实例
- 修复: 改按 app index 缓存, 每个 app 独立 screen
- 教训: 缓存键的选择决定行为语义。"按什么去重"必须匹配预期共享模型

**坑2: label 看不见(颜色撞背景)**
- 现象: settings_ui.json 里的 label 创建了但界面看不见
- 原因: label 没写 `color` → 用 LVGL 默认文字色(深色) → 深字深底(#1a1a2e) = 隐形
- 修复: ui_engine 给 label 默认白色文字(深色背景友好), JSON 可用 `color` 覆盖
- 教训: "看不见 ≠ 不存在"。控件消失先怀疑: ①颜色撞背景 ②位置出屏 ③被盖住 ④尺寸为0

---

## 六、热改实验(已验证)

改 JSON 不编译, 重启即生效:
- apps.json: 加/删 app, 改 name/symbol/color, 改 home 标题/背景
- settings_ui.json / wifi_ui.json: 加控件, 改位置/大小/文字/颜色, 改 slider 范围, 改 on_change 绑定

加新业务动作要编译(只改 ui_actions.c, 不改引擎)。

### 当前 app 列表
| App | 类型 | JSON/入口 | 动作 |
|-----|------|-----------|------|
| Music | builtin | lv_demo_music | - |
| Widgets | builtin | lv_demo_widgets | - |
| Settings | json | settings_ui.json | set_brightness, toggle_bluetooth |
| WiFi | json | wifi_ui.json | toggle_wifi, set_signal |

加新 app(如 Bluetooth/Display): 写 JSON + 加动作到 ui_actions.c, 不改引擎/launcher。

---

## 七、为什么没用 cJSON / JerryScript

- **cJSON**: 开发环境网络受限下不了, 自写 mini_json 更轻量且够用(若能联网可换 cJSON, 接口相似)
- **JerryScript**: 适合低频规则(AI响应/通知过滤/协议解析), 不适合高频 UI 回调。UI 布局用 JSON+C 足够。后续可加 JerryScript 做"规则引擎"层。

---

## 八、后续扩展方向

1. ui_engine 支持更多控件/布局(flex/grid/嵌套容器/字体)
2. ui_actions 加更多业务动作(音量/亮度/蓝牙/WiFi 真实驱动)
3. 加 JerryScript 规则引擎层(AI 眼镜自定义规则)
4. 移植到真实 MCU: JSON 放 SPI Flash + LittleFS, ui_engine 不变
5. **动态列表控件**: `list` 类型 + `ui_engine_list_add` API, 运行时数据(如蓝牙扫描结果)

---

## 九、子页面导航 — 通用框架(方案一: home 统一根)

### 架构演进

从"单层静态控件"升级到"列表 + 详情子页"的二级导航, 让框架能配任意设置类界面。

### 三处能力扩展

| 扩展 | 实现 |
|------|------|
| **动作带参数** | 签名 `void(*func)(lv_event_t*, const char* param)`; JSON 用冒号语法 `"on_click": "open_page:bt_page"`; ui_engine 用 trampoline(`action_binding_t` in user_data)桥接 LVGL cb 签名 |
| **list_item 控件** | 可点击行(图标+文字), 绑 `on_click`(CLICKED 事件), 用于设置菜单 |
| **控件 ID 查找** | `ui_engine_find_by_id(id)` — 建 控件时记录 id, C 动作可按 id 操作控件(为动态列表铺路) |

### 导航栈 (ui_nav 模块, 方案一)

**home 为统一栈根**, 所有页面(home/app首屏/子页)压在同一个栈里, Back 统一出栈一级。

```
home (depth 0, 根, 无Back)
  ↑ ui_nav_push_screen (进app)
app首屏 (depth 1, Back→home)
  ↑ ui_nav_open_page (进子页)
子页 (depth 2, Back→app首屏)
```

API:
- `ui_nav_set_root(home)` — 启动时设 home 为栈底
- `ui_nav_push_screen(scr)` — 压 app 首屏(非 JSON 来源)
- `ui_nav_open_page(id)` — 压子页(JSON 来源, 懒加载 `A:pages/<id>.json`, 缓存复用)
- `ui_nav_back()` — 出栈一级, 到 home 停
- `ui_nav_add_back_button(scr)` — 给非 home 页面加 Back 按钮

### 为什么选方案一(home 统一根)而非方案二(app 为根)

| 维度 | 方案一(home根) | 方案二(app根) |
|------|--------------|-------------|
| Back 逻辑 | 一套(统一出栈) | 两套(app回home + 子页回app) |
| 代码量 | 少(无重复back按钮) | 多(两套back/缓存) |
| 导航位置 | 不保留(回app到首屏) | 保留(回app还在原子页) |
| 跨app跳转 | 支持(通知→子页) | 不支持 |
| 状态保留 | ✅(screen缓存不销毁) | ✅ |

选方案一的理由: 代码简、回 app 看首屏符合预期、支持未来通知直达子页。状态保留两者都做得到。

### 关键认知: 清栈 ≠ 销毁

清栈(从栈移除)只是不显示, screen 对象仍在 `page_cache`/`json_screen_cache` 里。再次进入复用同一对象 → 控件状态保留。只有"导航位置"丢失(回 app 到首屏而非原子页), 这正是期望行为。

### 全局 page_cache

所有 app 的子页共享一个 `page_cache`(方案一统一栈的必然结果)。约定 **page_id 全局唯一**(`bt_page`/`wifi_page` 等天然不重名), 映射到 `A:pages/<id>.json`。同名 page_id = 同文件 = 同实例(合理)。

### 内置动作 vs 业务动作

| 类型 | 动作 | 谁提供 |
|------|------|--------|
| 内置(框架) | `open_page:<id>` / `back` | ui_actions.c, 所有 app 复用 |
| 业务(用户) | `toggle_bluetooth` / `set_brightness` / `toggle_hotspot` 等 | ui_actions.c, 接真实驱动 |

### 子页 JSON 文件 (pages/ 目录)

| 文件 | 内容 |
|------|------|
| pages/bt_page.json | 蓝牙开关 + 设备列表 |
| pages/brightness_page.json | 亮度 slider + 自动开关 |
| pages/battery_page.json | 电池信息(电量/状态/温度/健康) |
| pages/hotspot_page.json | 热点开关 + SSID/密码/连接数 |

settings_ui.json 改成 4 个 `list_item`, 各自 `on_click: open_page:xxx`。

### 配新 app 的流程(通用性)

1. 写 app 列表页 JSON(`list_item` + `on_click: open_page:xxx`)
2. 写各子页 JSON(放 `pages/`)
3. ui_actions.c 加业务动作
4. 运行 — 导航(open_page/back)是框架内置, 不改引擎/launcher/nav

### 文件清单(本次新增/改造)

| 文件 | 作用 |
|------|------|
| `src/ui_nav.h/.c` | 统一导航栈(home根 + push/pop + 子页懒加载缓存) |
| `src/ui_actions.h/.c` | 动作签名加 param; 加 open_page/back 内置动作 |
| `src/ui_engine.c` | list_item 控件; on_click 绑定; 冒号拆分 param; ID 查找表 |
| `src/app_launcher.c` | 用 ui_nav_push_screen; 删除重复 back 按钮; set_root(home) |
| `pages/*.json` | 4 个子页 |
| `settings_ui.json` | 改成 list_item 列表 |
