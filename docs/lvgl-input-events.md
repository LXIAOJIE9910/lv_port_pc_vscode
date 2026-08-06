# LVGL 输入事件机制 — Hit-test 与 Bubbling

> 基于 LVGL v9.6.0-dev (commit 40fb6ba)
> 源码: `lvgl/src/indev/lv_indev.c`, `lvgl/src/core/lv_obj.c`

---

## 一、两个独立机制,解决两个不同问题

| 机制 | 解决什么问题 | 类比 |
|------|-------------|------|
| **Hit-test (命中测试)** | "手指点下去,到底点中了谁?" | 像图层: 上层先被点中 |
| **Bubbling (冒泡)** | "点中子对象后,父对象要不要也收到事件?" | 像 DOM 事件冒泡,不是图层 |

它们是**先后两步**: 先 hit-test 找到目标,再决定事件是否冒泡。两者正交:
- Hit-test: **谁是被点中的目标**(决定事件起始点)
- Bubbling: **目标之外,祖先要不要也收到**(决定事件传播范围)

---

## 二、Hit-test: 类似图层

### 核心函数 `lv_indev_search_obj` (递归)

手指按下时, LVGL 从屏幕根对象开始, **递归找最顶层的命中对象**:

```c
lv_obj_t * lv_indev_search_obj(lv_obj_t * obj, lv_point_t * point)
{
    if(lv_obj_is_hidden(obj)) return NULL;              // 隐藏的直接跳过
    bool hit_test_ok = lv_obj_hit_test(obj, &p_trans);  // 本身可点?

    if(点在本对象区域内) {
        /* ★从最后一个子对象往前遍历(i--递减) */
        for(i = child_cnt - 1; i >= 0; i--) {
            found_p = lv_indev_search_obj(child, &p_trans);
            if(found_p) return found_p;                 // 子对象命中就用它, 立即返回
        }
    }
    if(hit_test_ok) return obj;                         // 子对象没命中, 用自己
    else return NULL;                                   // 自己也不可点, 返回NULL(穿透)
}
```

### "图层"体现在两个地方

**① 子对象倒序遍历 (`i = child_cnt-1; i >= 0; i--`)**

子对象列表里, **后创建的(index 大)在前**。倒序遍历 = 先检查"上层"。所以:
- 后创建的子对象 = 视觉上盖在前面 = 优先被命中
- 这就是"图层叠放,上层先被点中"

**② `clickable=false` 实现穿透**

`lv_obj_hit_test` 内部检查 `clickable` 标志。若对象 `clickable=false`, `hit_test_ok` 为 false, 函数返回 NULL —— **等于这一层不存在, 触摸"穿"到下面去**。

### Music demo 的穿透示例

```
screen (clickable=false, 透传)
  └─ main_cont (clickable=false, 透传)
       └─ player (clickable=true, 可命中)
            └─ spectrum_obj (clickable=false, 透传)  ← 手指穿过去
                 └─ album_image_obj (clickable=true, 可命中) ← 命中这里!
```

`spectrum_obj` 设 `clickable=false` 的意义: **让频谱画板对触摸"透明", 手指能点中下面的封面**。若它 `clickable=true`, 会"吃掉"触摸, 封面收不到手势。

### Hit-test 流程图

```
手指按下 (x,y)
   │
   ▼ 从 screen 根节点递归
lv_indev_search_obj(screen, point)
   │
   ├─ 点在 screen 区域? 是
   │   ├─ 倒序遍历子对象
   │   │   └─ child_N (最上层) → 递归
   │   │        ├─ 点在其区域? 是
   │   │        ├─ clickable? 否 → 本层穿透
   │   │        ├─ 倒序遍历它的子对象
   │   │        │   └─ album_image_obj
   │   │        │        ├─ 点在区域? 是
   │   │        │        ├─ clickable? 是 ★命中!
   │   │        │        └─ 无子对象 → return album_image_obj
   │   │        └─ 子对象命中, 立即返回
   │   └─ ...
   │
   ▼ 最终目标: album_image_obj
```

**所以 hit-test 确实像图层**: 后创建的在上层优先命中, clickable=false 的层可被穿透, 上层命中就返回不再往下找。

### 触摸如何判定"点中了 slider 还是 switch" — 坐标命中,无需"选中"

**核心认知: 触摸没有"选中"概念,只有"命中"。** 手指落下的坐标 (x,y) 决定一切——落在哪个控件的矩形区域内,就命中谁。这是纯几何判定, 不需要 group, 不需要"记住当前对象"。

判定逻辑 (`lv_indev.c`):
```c
bool hit_test_ok = lv_obj_hit_test(obj, &p_trans);          // 这个对象可点吗(clickable)?
if(lv_area_is_point_on(&obj_coords, &p_trans, 0)) {         // 坐标在对象矩形内吗?
    for(i = child_cnt - 1; i >= 0; i--) {                   // 递归检查子对象(上层优先)
        found_p = lv_indev_search_obj(child, &p_trans);
        if(found_p) return found_p;                         // 子对象命中就返回
    }
}
if(hit_test_ok) return obj;                                 // 否则用自己
```

**具体场景: 音量 slider 和蓝牙 switch 并排**

假设布局:
```
屏幕
├── 音量 slider   坐标区域 (x: 50~250, y: 100~130)   clickable=true
└── 蓝牙 switch   坐标区域 (x: 300~400, y: 100~130)   clickable=true
```

手指点在 (150, 115):
```
(150,115) 在 slider 区域(50~250,100~130)内?  ✅ 是 → 命中 slider
(150,115) 在 switch 区域(300~400,100~130)内? ❌ 否
→ 事件发给 slider → slider 默认行为(x坐标换算成value) → 你的 volume_cb
```

手指点在 (350, 115):
```
(350,115) 在 slider 区域内? ❌ 否
(350,115) 在 switch 区域内? ✅ 是 → 命中 switch
→ 事件发给 switch → switch 默认行为(翻转CHECKED) → 你的 ble_cb
```

**LVGL 不需要"知道你选中了 slider 还是 switch"** —— 它只算坐标落在哪个矩形里, 然后把事件发给那个对象。是 slider 还是 switch, 是对象自己的默认行为和回调决定的, 不是 LVGL 提前判断的。

### 命中对象后,行为由"对象类型"决定(不是 hit-test)

hit-test 只负责"找到对象", 找到之后:

| 命中对象 | 对象的默认行为 | 触发的事件 | 你的回调 |
|----------|---------------|-----------|----------|
| slider | x坐标→value | VALUE_CHANGED | volume_cb → 调音量 |
| switch | 翻转CHECKED | VALUE_CHANGED | ble_cb → 开关蓝牙 |
| button | 高亮按下态 | CLICKED | btn_cb → 跳转 |

同一个"点中"动作, 因命中的对象类型不同, 后续行为完全不同。但 hit-test 这一步对所有对象统一——就是坐标 vs 矩形。

### 触摸 vs 键盘: "找到对象"的两种方式对比

这是触摸和键盘/旋钮输入的本质区别:

```
┌─────────────────────────────────────────────────────┐
│  触摸: 怎么知道点的是slider还是switch?                │
│                                                      │
│  手指(150,115)                                       │
│       │                                              │
│       ▼ lv_indev_search_obj (递归hit-test)           │
│  遍历screen的子对象:                                  │
│    slider的区域含(150,115)? ✅ 命中! → 返回slider    │
│    (switch都不用看了)                                 │
│       │                                              │
│       ▼ 事件发给slider                               │
│  slider默认行为 + 你的volume_cb                      │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│  键盘: 怎么知道操作的是slider还是switch?              │
│                                                      │
│  按Tab键                                              │
│       │                                              │
│       ▼ group里焦点指针 +1                           │
│  group = [slider, switch, ...]                       │
│           ↑焦点                                       │
│       │                                              │
│       ▼ 焦点移到slider                               │
│  按Enter → 操作group里当前焦点对象(slider)            │
│       │                                              │
│       ▼ 事件发给slider                               │
│  slider默认行为 + 你的volume_cb                      │
└─────────────────────────────────────────────────────┘

  两种方式最后都到"事件发给slider"这一步, 之后完全一样
  区别只在"怎么找到slider": 触摸靠坐标, 键盘靠焦点
```

**group 的本质是"给无坐标输入设备维护一个'当前对象'指针"。** 键盘/旋钮没有坐标, 只能靠焦点"记住"当前对象; 触摸有坐标, 每次按下都能直接算出对象, 不需要这个指针。所以:

| 输入方式 | 有坐标? | 找对象的方式 | 需要 group? |
|----------|---------|-------------|------------|
| 触摸/鼠标 | ✅ 有 | hit-test 坐标命中 | ❌ 不需要 |
| 键盘/旋钮 | ❌ 没有 | group 焦点移动 | ✅ 必须 |

### 触摸的 focused 态(容易混淆)

虽然触摸不靠 group 导航, 但**点中一个控件时, LVGL 也会给它加 `LV_STATE_FOCUSED`** (如果设了 `click_focusable`)。这是为了视觉反馈(点中的控件高亮), 不是用于导航。触摸的焦点是"刚才点的那一下"产生的, 和 group 里 Tab 移过来的焦点是两回事。

---

## 三、Bubbling: 和图层无关, 是事件"向上通知"

Hit-test 找到目标后, 事件先发给目标。但有时父对象也想"知道"这个事件 —— 这就是冒泡。LVGL 有两种冒泡标志:

### ① `gesture_bubble` (手势冒泡)

控制**手势事件**是否向父对象传递。`indev_gesture` 实现:

```c
static void indev_gesture(lv_indev_t * indev)
{
    lv_obj_t * gesture_obj = indev->pointer.act_obj;  // 从命中对象开始

    /* ★沿父链向上找, 直到遇到 gesture_bubble=false 的对象 */
    while(gesture_obj && lv_obj_is_gesture_bubble(gesture_obj)) {
        gesture_obj = lv_obj_get_parent(gesture_obj);
    }
    // 最终 gesture_obj 就是"处理手势的对象"
    ...
}
```

**机制**: 从命中的子对象开始, 沿父链往上走, **只要沿途 `gesture_bubble=true` 就继续往上**, 遇到 `false` 停下来 —— 那个对象处理手势。

Music demo 封面设了 `gesture_bubble=false`:
```c
lv_obj_add_event_cb(img, album_gesture_event_cb, LV_EVENT_GESTURE, NULL);
lv_obj_set_gesture_bubble(img, false);   // ★手势到我就停, 不再往上
```
意思是"手势我自己处理, 别传给父对象 spectrum_obj / player"。若设 true, 手势会冒泡到父对象, 封面自己的回调就不一定收到了。

### ② `event_bubble` (普通事件冒泡)

控制**所有事件** (PRESSED/CLICKED 等) 是否向父链传递。设 `lv_obj_set_parent_bubble(obj, true)` 后, 事件发给子对象的同时, 也会发给父、祖父……一路向上。

这和 DOM 的 `addEventListener` 冒泡完全一样: 点了按钮, 按钮的父容器、祖父容器都能收到 click 事件。

### 冒泡不是"图层"

冒泡是**事件传递方向** (子→父, 向上), 不是"谁在上层"。一个事件冒泡时, 它已经经过了 hit-test 确定目标, 冒泡只是"顺便通知祖先"。

---

## 四、两者协作的完整流程

```
手指按下并滑动
   │
   ▼ ① Hit-test (找目标, 像图层)
   从 screen 递归倒序找 → 命中 album_image_obj (clickable=true)
   indev->pointer.act_obj = album_image_obj
   │
   ▼ ② 事件发给目标
   PRESSED/PRESSING/RELEASED 事件发给 album_image_obj
   │
   ▼ ③ 手势判定 (滑动超过阈值)
   indev_gesture():
     从 act_obj 开始, 沿父链找 gesture_bubble=false 的对象
     album_image_obj.gesture_bubble == false → 停在这里
     → 发 LV_EVENT_GESTURE 给 album_image_obj
   │
   ▼ ④ 封面回调处理
   album_gesture_event_cb:
     左滑 → album_next(true)  切下一首
     右滑 → album_next(false) 切上一首
```

若封面 `gesture_bubble=true` (默认), 手势会冒泡到 `spectrum_obj` → `player` → `main_cont`, 最终可能被 `main_cont` 当成滚动处理 (上下切页面), 封面的左右滑切歌就失效了。所以设 `false` 是为了"独占"手势。

---

## 五、关键标志位速查

| 标志 | 设置函数 | 作用 |
|------|----------|------|
| `clickable` | `lv_obj_set_clickable` | 是否参与 hit-test (false=穿透) |
| `scrollable` | `lv_obj_set_scrollable` | 是否可滚动 (独立于 clickable) |
| `gesture_bubble` | `lv_obj_set_gesture_bubble` | 手势事件是否向父冒泡 |
| `event_bubble` | `lv_obj_set_parent_bubble` | 普通事件是否向父冒泡 |
| `scroll_chain_hor/ver` | `lv_obj_set_scroll_chain_*` | 滚动到边界是否链给父 |
| `adv_hittest` | (扩展) | 自定义精确命中测试 |
| `press_lock` | | 按下后锁定目标,滑动不切换 |

**注意**: `clickable` 和 `scrollable` 是**独立**属性。一个对象可 `clickable=false` 但仍 `scrollable` (如 `main_cont`: 不响应点击但能滚动)。滚动由内容超出 + 手势驱动, 不要求 clickable。

---

## 六、触摸屏接入: UI 代码完全不碰 touch

**核心认知: 接入触摸屏后, UI 代码只管"控件 + 回调 + 业务", 完全不碰 touch。** 触摸相关的逻辑只出现在一处——read_cb, 它替代 `sdl_hal_init` 里的 `lv_sdl_mouse_create`。

### 分层架构

```
┌─────────────────────────────────────────────┐
│  你的 UI 代码                                  │  ← 你只管这层
│  create 控件 + 挂回调 + 回调里做业务           │
├─────────────────────────────────────────────┤
│  LVGL 核心 (hit-test/事件/绘制/刷新)          │  ← LVGL 全包
├─────────────────────────────────────────────┤
│  触摸 indev 适配 (read_cb 读触摸坐标)          │  ← 你只写这一小段
├─────────────────────────────────────────────┤
│  触摸硬件 (I2C/SPI 触摸IC)                    │
└─────────────────────────────────────────────┘
```

### 唯一需要写的触摸代码: read_cb

```c
/* read_cb: 被LVGL周期性调用, 把硬件触摸坐标填进lv_indev_data_t */
static void touchpad_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    int16_t x, y;
    bool pressed = ft6336_read(&x, &y);   /* 读你的触摸IC, 返回是否按下+坐标 */

    if(pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;                /* 把坐标告诉LVGL */
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* 在hal_init里注册(替代lv_sdl_mouse_create) */
lv_indev_t * indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, touchpad_read_cb);
lv_indev_set_display(indev, disp);
```

这段代码只做一件事: 定期被 LVGL 调用, 把硬件触摸坐标填进 `lv_indev_data_t`。之后的命中判定、事件派发、UI 更新——全是 LVGL 自动完成, UI 代码一行都不用改。

### 责任边界

| 谁 | 负责什么 |
|------|----------|
| **你写 read_cb** | 读触摸 IC → 填坐标和按下状态 |
| **LVGL 自动** | 调 read_cb → 坐标 hit-test 命中谁 → 该控件默认行为 → 发事件 |
| **你的 UI 回调** | 收到事件 → 做业务(如调音量) |
| **LVGL 自动** | 标记脏 → 刷新重绘 → 推屏 |

touch 相关逻辑**只出现在 read_cb 这一处**, UI 代码和它完全隔离。`sdl_hal_init` 里的 `lv_sdl_mouse_create` 内部也是注册了一个读 SDL 鼠标的 read_cb, 本质一模一样。

### 触摸 = 鼠标: 同一种输入类型

把触摸屏想成**鼠标**——在 PC 上写 UI 时从来不关心"鼠标怎么移动、怎么点击", 只管 `lv_obj_add_event_cb(btn, cb, CLICKED, NULL)`。触摸屏同理: LVGL 把它抽象成 `LV_INDEV_TYPE_POINTER`, 和鼠标是**同一种输入类型**。所以 PC 模拟器上调好的 UI, 换到触摸屏上行为完全一样。

> 这就是为什么先在 PC 上开发再移植能省大量调试时间: PC 模拟器的 SDL 鼠标和真实触摸屏, 对 LVGL 来说是同一个东西。

### 唯一要注意: 坐标校准

电阻屏/某些电容屏的原始坐标可能需要校准(缩放/旋转/镜像), 这也是 read_cb 里的活, UI 代码不用管:

```c
data->point.x = (int32_t)raw_x * SCREEN_W / 4096;   /* 12bit ADC → 屏幕像素 */
data->point.y = (int32_t)raw_y * SCREEN_H / 4096;
/* 如果屏是横装的但触摸是竖向, 还要交换x/y */
```

校准好之后, LVGL 拿到的就是正确的屏幕坐标, hit-test 正常工作。

### 移植时的唯一改动

`sdl_hal_init` 里这一行:
```c
lv_indev_t * mouse = lv_sdl_mouse_create();          // PC模拟器: SDL鼠标
```
换成:
```c
lv_indev_t * indev = lv_indev_create();              // 硬件: 自定义触摸
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, touchpad_read_cb);
lv_indev_set_display(indev, disp);
```

**上层 UI 代码一个字都不用改** —— widgets/music/keypad_encoder 等 demo 都能原样跑在真实触摸屏上。这就是 `hal.c` 作为"接缝"的价值: 换底层, 不动上层。

---

## 七、比喻总结

- **Hit-test = 你点的是哪张纸** (最上面那张被点中, 透明的纸可看穿)
- **Bubbling = 点了纸之后, 要不要告诉下面垫着的纸** (这张纸下面垫着的纸要不要也知道你点了)

**Hit-test 像图层** —— 递归倒序遍历子对象 (后创建在上层优先), `clickable=false` 的对象可被触摸穿透。这就是 `spectrum_obj` 设不可点让手指能点到封面的原因。

**Bubbling 不是图层** —— 是事件沿父链向上传播的机制 (`gesture_bubble`/`event_bubble`), 封面设 `gesture_bubble=false` 是为了"独占"手势不让它冒泡到父容器被当滚动处理。

两者正交: hit-test 决定事件给谁, bubbling 决定祖先要不要也收到。

---

## 附: 相关源码位置

- `lv_indev_search_obj` — `lvgl/src/indev/lv_indev.c:610` (hit-test 递归)
- `indev_gesture` — `lvgl/src/indev/lv_indev.c:1796` (手势冒泡循环)
- `lv_obj_hit_test` / `lv_obj_is_gesture_bubble` — `lvgl/src/core/lv_obj.c`
- 标志位存储在 `lv_obj_t` 的位域 (clickable/scrollable/gesture_bubble/event_bubble 等)
