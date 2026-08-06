# LVGL Music Demo — UI 设计逻辑与调用流程

> 基于 `lvgl/demos/music/` (LVGL v9.6.0-dev, commit 40fb6ba)
> 横屏模式 `LV_DEMO_MUSIC_LANDSCAPE=1`,分辨率 480×272

---

## 一、整体架构:三层分离

音乐播放器 demo 体现了 LVGL 的核心设计哲学——**关注点分离 / 数据驱动视图**。整个系统分三层,通过全局变量解耦协作:

```
┌─────────────────────────────────────────────────────┐
│  交互层  (事件回调)                                   │
│  next_click_event_cb / play_event_click_cb / ...     │  ← 响应用户操作
├─────────────────────────────────────────────────────┤
│  业务层  (播放控制)                                   │
│  lv_demo_music_play / pause / resume / album_next    │  ← 状态机 + 动画驱动
│  track_load                                          │
├─────────────────────────────────────────────────────┤
│  视图层  (控件 + 绘制)                                │
│  create_xxx 构建控件 / spectrum_draw_event_cb 绘制    │  ← 状态驱动重绘
└─────────────────────────────────────────────────────┘
       ↑                          ↓
       └──── 全局变量桥梁 ────────┘
   title_label / album_image_obj / spectrum / spectrum_i / play_obj ...
```

**三层互不直接调用,通过共享全局变量协作。** 改状态(数据)→ 标记脏 → LVGL 刷新时自动重绘。

---

## 二、界面分区:7 个 create_xxx 各管一块

`lv_demo_music_main_create` 调用 7 个函数构建界面,每个负责一个矩形区块,最后用网格拼装:

```
┌─────────────────────────────────────────────┐ ← player 容器(白底圆角)
│  ░░░ create_wave_images: 顶部波浪纹理 ░░░      │  (ignore_layout, 绝对贴边)
│                                             │
│        ╔═══════════════════════════╗         │
│        ║   create_title_box        ║         │  ③ 歌名/作者/流派 (3个label)
│        ║   "Waiting for true love" ║         │
│        ║   "The Fleming"           ║         │
│        ║   "Pop / 2020"            ║         │
│        ╚═══════════════════════════╝         │
│                                             │
│  ◐ ◑ ◒ ◓  create_icon_box: 4个功能图标       │  ④ 纯装饰,无回调
│                                             │
│        ╔═══════════════════════════╗         │
│        ║      [频谱柱围成一圈]       ║         │
│        ║   ⑤ create_spectrum_obj   ║         │  自定义绘制 + 中心封面
│        ║      [封面在中心脉动]       ║         │
│        ╚═══════════════════════════╝         │
│                                             │
│  [随机][◀][▶][▶▶][循环]  ⑥ create_ctrl_box  │  5按钮(3个可点)+进度条+时间
│  [═════进度条══════] 0:00                    │
│                                             │
│  ── ALL TRACKS ──  ⑦ create_handle           │  滚动手柄(下拉进列表)
│  ░░░ create_wave_images: 底部波浪+圆角 ░░░     │
└─────────────────────────────────────────────┘
     ↑ ① create_cont: 整个 player 在 main_cont 里可上下滚动
```

### 各函数职责对照

| # | 函数 | 构建的控件 | 交互性 | 实现方式 |
|---|------|-----------|--------|----------|
| ① | `create_cont` | main_cont + player 容器 | 滚动+中心吸附 | 布局骨架,透明容器 |
| ② | `create_wave_images` | 4张装饰图(上下波浪+左右圆角) | ❌ 纯装饰 | `lv_image`, `ignore_layout` |
| ③ | `create_title_box` | 3个 label(歌名/作者/流派) | 文本可更新 | `lv_label`, flex纵向 |
| ④ | `create_icon_box` | 4个功能图标 | ❌ 纯装饰 | `lv_image`, flex横向 |
| ⑤ | `create_spectrum_obj` | 频谱容器 + 中心封面 | 封面可手势 | **自定义绘制** |
| ⑥ | `create_ctrl_box` | 5按钮 + 进度条 + 时间label | ✅ 3按钮可点 | image/imagebutton/slider |
| ⑦ | `create_handle` | "ALL TRACKS" 文字+横条 | ❌ 视觉提示 | label + obj |

---

## 三、核心设计:三种控件更新策略

切歌时不同控件用不同方式更新,这是 LVGL 的典型模式:

### 策略 A:文本类 — 原地改数据 (title/作者/流派)

```c
lv_label_set_text(title_label, lv_demo_music_get_title(track_id));
```
- 对象不变,只改字符串
- label 内部自动标记脏 → 下次刷新重绘
- 适用于:状态简单变化,无需动画

### 策略 B:图片类 — 销毁重建 + 动画过渡 (封面)

```c
// 旧封面: 淡出+滑走+缩小, 动画完成自动删除
lv_anim_set_completed_cb(&a, lv_obj_delete_anim_completed_cb);
// 新封面: 创建+放大+淡入
album_image_obj = album_image_create(spectrum_obj);
```
- 旧对象带"死亡动画",完成后自动销毁
- 新对象创建,带"入场动画"
- 适用于:资源不同 + 需要过渡动画

### 策略 C:绘制类 — 改数据源 (频谱)

```c
// album_image_create 内部
spectrum = spectrum_2;   // 只改全局指针
```
- 绘制逻辑无状态,每次重绘读当前指针
- 改数据源后,下次刷新自动用新数据画
- 适用于:自定义绘制的内容切换

---

## 四、关键:数据驱动绘制的协作机制

频谱可视化是整个 demo 最精妙的部分,三个函数通过两个全局变量协作:

### 三个函数的分工

```
album_image_create    →  准备"用什么数据画"   (改 spectrum 指针 = 数据源)
spectrum_anim_cb      →  决定"画第几帧"       (改 spectrum_i = 时间推进)
spectrum_draw_event_cb →  真正"把数据画出来"  (读 spectrum[spectrum_i] 绘制)
```

**三者互不调用,通过共享全局变量协作:**

```c
static const uint16_t (* spectrum)[4];   // 数据源: 当前歌的频谱数组
static uint32_t spectrum_i = 0;          // 时间索引: 当前播放到第几帧
```

### 绘制触发机制

`spectrum_draw_event_cb` 用 `LV_EVENT_ALL` 订阅,被动响应 LVGL 刷新:

```
spectrum_i 被动画推进 / spectrum_obj 内容变化
   ↓
spectrum_obj 被标记为脏
   ↓
下一个刷新周期 (refr timer, 33ms)
   ↓
LVGL 发事件给 spectrum_obj:
   ├─ LV_EVENT_REFR_EXT_DRAW_SIZE  → 设扩展绘制范围(频谱柱越界)
   ├─ LV_EVENT_COVER_CHECK         → 报告"不完全覆盖"(柱间有空隙)
   └─ LV_EVENT_DRAW_MAIN_BEGIN     → ★读 spectrum[spectrum_i] 画20根三角柱
   ↓
flush_cb → 屏幕
```

**关键认知:绘制回调是无状态的、被动的。** 它不知道谁改了数据,只认当前 `spectrum` 和 `spectrum_i`。数据一改,下次重绘自然反映——这就是"状态驱动视图"。

---

## 五、完整调用流程图:点击"下一首"

```
用户点击"下一首"按钮
   │
   ▼ ① 按钮事件回调 (LVGL indev → CLICKED)
next_click_event_cb(e)
   └─ if(code == LV_EVENT_CLICKED)
        └─ lv_demo_music_album_next(true)
   │
   ▼ ② 业务层:计算下一首ID
lv_demo_music_album_next(true)            [lv_demo_music.c]
   └─ id = (track_id + 1) % ACTIVE_TRACK_CNT
      └─ lv_demo_music_play(id)
   │
   ▼ ③ 播放 = 装载 + 恢复
lv_demo_music_play(id)
   ├─► track_load(id)                     ★ 装载新曲目
   │     │
   │     ├─ 重置: spectrum_i=0, time_act=0, 进度条=0, 时间="0:00"
   │     │
   │     ├─ 更新播放列表选中态:
   │     │   lv_demo_music_list_button_check(track_id, false)  旧歌取消选中
   │     │   lv_demo_music_list_button_check(id, true)         新歌选中
   │     │
   │     ├─ 【策略A】改文本 (原地更新):
   │     │   lv_label_set_text(title_label,  get_title(id))   ─► [脏] ─► 重绘
   │     │   lv_label_set_text(artist_label, get_artist(id))  ─► [脏] ─► 重绘
   │     │   lv_label_set_text(genre_label,  get_genre(id))   ─► [脏] ─► 重绘
   │     │
   │     ├─ 【策略B】旧封面退场动画:
   │     │   album_image_obj 淡出 + 横向滑走 + 缩小
   │     │   completed_cb = lv_obj_delete_anim_completed_cb  (动画完自动删除)
   │     │
   │     ├─ 【策略B+C】新封面入场 + 切换数据源:
   │     │   album_image_obj = album_image_create(spectrum_obj)
   │     │     ├─ 按 id 选 cover_1/2/3 图
   │     │     ├─ spectrum = spectrum_1/2/3        ★频谱数据源切换
   │     │     └─ 居中对齐 + 手势回调
   │     │   新封面: 延迟100ms 放大 + 淡入
   │     │
   │     └─ (track_load 结束)
   │
   └─► lv_demo_music_resume()              ★ 启动播放
         ├─ playing = true
         ├─ 启动 spectrum_anim 动画
         │   └─ spectrum_anim_cb 每30ms推进 spectrum_i
         │        └─ invalidate(spectrum_obj)
         ├─ 恢复 sec_counter_timer (每秒推进 time_act/进度条)
         └─ lv_obj_add_state(play_obj, LV_STATE_CHECKED)  图标变暂停
   │
   ▼ ④ 之后每个刷新周期 (refr timer 33ms)
LVGL 自动重绘所有脏区域:
   ├─ title_label 重绘    → 新歌名显示
   ├─ artist_label 重绘   → 新作者显示
   ├─ genre_label 重绘    → 新流派显示
   ├─ album_image_obj 重绘 → 新封面显示 (带动画过渡)
   └─ spectrum_obj 重绘   → 发 DRAW_MAIN_BEGIN
        └─ spectrum_draw_event_cb 读 spectrum[spectrum_i]
             ├─ spectrum 已是新数据源 ✓
             ├─ spectrum_i 持续推进 ✓
             └─ 画出新歌的频谱柱跳动 ✓
   │
   ▼ ⑤ 屏幕呈现
新歌名 + 新封面 + 新频谱律动 + 播放键变暂停图标
```

---

## 六、关键设计模式总结

### 1. 全局变量作为对象句柄桥梁
```c
static lv_obj_t * title_label;      // create_title_box 创建时赋值
static lv_obj_t * album_image_obj;  // track_load 切歌时重新赋值
static lv_obj_t * play_obj;         // resume/pause 远程改状态
```
`create_xxx` 存对象到全局,其他函数取出来改。这是连接"创建时"和"更新时"的桥梁。

### 2. 状态驱动视图 (不用手动绘制)
```
改状态 (set_text / add_state / 改指针)
   → 控件自动标记脏
   → LVGL 刷新周期重绘
   → flush_cb 上屏
```
你只管"控件应该是什么状态",不管"怎么画出来"。

### 3. 事件订阅过滤 (LV_EVENT_ALL)
```c
lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, NULL);
// LV_EVENT_ALL 不是事件,是"接收所有事件"的订阅标志
// 回调内必须 if(code == XXX) 分发
```

### 4. 完成即删除的动画惯用法
```c
lv_anim_set_completed_cb(&a, lv_obj_delete_anim_completed_cb);
// 动画播放完,对象自动销毁,无需手动 lv_obj_del
```
用于"退场动画":旧封面滑走 → 自动删除。

### 5. 自定义绘制的事件钩子
```c
LV_EVENT_REFR_EXT_DRAW_SIZE  → 申请越界绘制(频谱柱超出容器)
LV_EVENT_DRAW_MAIN_BEGIN     → 拦截绘制,自己用 lv_draw_triangle 画
```
无需重写控件源码,只在绘制事件里插入自定义内容。

### 6. 数据源切换 + 无状态绘制
```c
// 切数据源 (一处)
spectrum = spectrum_2;

// 绘制读当前数据源 (另一处, 无状态)
ampl = spectrum[spectrum_i][s];
```
绘制逻辑只读不存,数据源一改,下次重绘自动反映。两者解耦。

---

## 七、交互按钮的可点性来源

| 按钮 | 创建方式 | 回调 | clickable | 能点 |
|------|----------|------|-----------|------|
| 随机(rnd) | lv_image | ❌ | ❌ | 否 |
| 上一首(prev) | lv_image | ✅ prev_click_event_cb | ✅ | 是 |
| 播放/暂停 | lv_imagebutton | ✅ play_event_click_cb | ✅ | 是 |
| 下一首(next) | lv_image | ✅ next_click_event_cb | ✅ | 是 |
| 循环(loop) | lv_image | ❌ | ❌ | 否 |

**能点与否取决于"有没有挂回调 + 设 clickable",与控件类型无关。** 播放键用 imagebutton 是因为要支持双状态图标切换(播放/暂停),配合 set_checkable + 状态图自动切换。

---

## 八、播放/暂停图标自动切换机制

```c
// 创建时配置状态图
lv_imagebutton_set_src(play_obj, RELEASED, ..., btn_play, ...);         // 未勾选→播放图标
lv_imagebutton_set_src(play_obj, CHECKED_RELEASED, ..., btn_pause, ...);// 勾选→暂停图标
lv_obj_set_checkable(play_obj, true);
```

点击时:
```
LVGL 默认行为: 翻转 LV_STATE_CHECKED
   → imagebutton 默认绘制: 按状态选图标
   → 重绘
   → 图标自动变
```
**回调只管业务(启停动画),不碰图标。** 图标切换完全由 LVGL 内部完成。

---

## 附:相关文件

- 入口: `lvgl/demos/music/lv_demo_music.c` — `lv_demo_music()` / `album_next()` / `play()`
- 主界面: `lvgl/demos/music/lv_demo_music_main.c` — 所有 create_xxx / 事件回调 / 频谱绘制
- 播放列表: `lvgl/demos/music/lv_demo_music_list.c`
- 频谱数据: `lvgl/demos/music/assets/spectrum.py` (离线生成 spectrum_1/2/3 数组)
- 配置: `lv_conf.h` — `LV_DEMO_MUSIC_LANDSCAPE` / `AUTO_PLAY` 等开关
