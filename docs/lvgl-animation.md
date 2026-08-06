# LVGL 动画系统 — `lv_anim_t` 与执行回调

> 基于 LVGL v9.6.0-dev (commit 40fb6ba)
> 源码: `lvgl/src/misc/lv_anim.c`, music demo `lvgl/demos/music/lv_demo_music_main.c`

---

## 一、核心认知:动画是"独立个体"

每个 `lv_anim_start(&a)` 创建一个**独立的动画对象**,注册进 LVGL 的动画链表。每个动画有自己的:
- `var` — 作用对象
- `exec_cb` — 执行回调
- `values` — 起止值
- `duration` — 时长
- `delay` — 延迟
- `path_cb` — 插值曲线(线性/ease_out/overshoot 等)

**关键纠正**: 不是"一帧把所有回调都执行一遍"。LVGL 动画管理器(由 `lv_timer_handler` 驱动)每帧遍历**所有仍在运行的动画**,按各自进度算当前值,调各自的 `exec_cb`。

```
每帧(tick) anim_timer 遍历所有活跃动画:
   动画A (spectrum, spectrum_anim_cb)        进度30% → v=300 → 调 spectrum_anim_cb(obj, 300)
   动画B (封面X, _obj_set_x_anim_cb)          进度60% → v=-100 → 调 _obj_set_x_anim_cb(封面, -100)
   动画C (封面scale, _image_set_scale_anim_cb) 进度10% → v=270 → 调 ...
   动画D (封面opa, album_fade_anim_cb)        进度90% → v=25 → 调 ...
   ...
```

每个动画"是否执行"取决于它**自己是否还在运行**(未到 duration),而不是"该轮到谁"。多个动画可并行(如切歌时 5 个封面动画同时跑),各改各的属性互不干扰。

---

## 二、两种"帧"的区别

容易混淆是因为有两个不同的"帧":

| 概念 | 频率 | 驱动 | 做什么 |
|------|------|------|--------|
| **刷新帧** (refr) | ~30fps (33ms) | `lv_timer_handler` → refr timer | 重绘脏区域到屏幕 |
| **动画帧** (anim tick) | ~30fps (默认) | `lv_timer_handler` → anim timer | 推进所有动画,调各自 exec_cb |

两者都由 `lv_timer_handler` 驱动,频率接近,但**职责不同**:
- 动画帧: 算"现在属性该是什么值" → 调 exec_cb 改属性
- 刷新帧: 把改过的属性画出来

---

## 三、`lv_anim_set_exec_cb` 注册的回调类型

### 1. 适配器回调 (Adapter) — 单行类型转换

LVGL 动画的 exec_cb 签名固定为 `void (*)(void * var, int32_t v)` (对象指针 + 当前值),但 `lv_obj_set_x` / `lv_image_set_scale` 等函数参数类型各不相同。所以包一层适配器:

```c
/* 只是转个类型, 避免编译警告 */
static void _obj_set_x_anim_cb(void * obj, int32_t x) {
    lv_obj_set_x((lv_obj_t *)obj, (int32_t)x);
}
static void _image_set_scale_anim_cb(void * obj, int32_t scale) {
    lv_image_set_scale((lv_obj_t *)obj, (uint16_t)scale);
}
static void album_fade_anim_cb(void * var, int32_t v) {
    lv_obj_set_style_image_opa(var, v, 0);
}
```

代码注释直白: *"Callback adapter function to convert parameter types to avoid compile-time warning"*。

### 2. 业务回调 — 含实际逻辑

`spectrum_anim_cb` 不是简单适配器,内有业务逻辑:

```c
static void spectrum_anim_cb(void * a, int32_t v)
{
    lv_obj_t * obj = a;
    if(start_anim) { lv_obj_invalidate(obj); return; }  // 入场动画阶段直接返回

    spectrum_i = v;                 // ★推进时间索引
    lv_obj_invalidate(obj);         // ★标记脏 → 触发重绘

    /* 低音检测 → 频谱柱游走偏移 */
    if(spectrum[spectrum_i][0] > 12) {                   // [0]是低音能量
        if(spectrum_i - last_bass > 5) {                 // 距上次低音>5帧
            bass_cnt++;
            last_bass = spectrum_i;
            if(bass_cnt >= 2) { bar_ofs++; bass_cnt=0; } // 强拍偏移
        }
    }
    if(spectrum[spectrum_i][0] < 4) bar_rot += dir;      // 无低音时walking效果

    /* 封面随低音脉动 */
    lv_image_set_scale(album_image_obj, LV_SCALE_NONE + spectrum[spectrum_i][0]);
}
```

---

## 四、`spectrum_anim_cb`: 指挥而非画师

这个函数是整个频谱律动的"心脏",但它**一行绘制代码都没有**。它只做两类事:
1. **改数据**: `spectrum_i`、`bar_ofs`、`bar_rot`、封面 scale
2. **触发重绘**: `lv_obj_invalidate`

真正画频谱柱的是 `spectrum_draw_event_cb`,画封面的是 image 默认绘制。`spectrum_anim_cb` 只是按时间节奏"喂"新数据。

### 注册处(`lv_demo_music_resume`)

```c
lv_anim_set_values(&a, spectrum_i, spectrum_len - 1);  // 从当前帧到最后一帧
lv_anim_set_exec_cb(&a, spectrum_anim_cb);             // ★每帧调这个
lv_anim_set_var(&a, spectrum_obj);
lv_anim_set_duration(&a, ((spectrum_len - spectrum_i) * 1000) / 30);  // 总时长(30fps)
lv_anim_set_completed_cb(&a, spectrum_end_cb);         // 播完调这个
lv_anim_start(&a);
```

LVGL 动画在 `spectrum_i` 到 `spectrum_len-1` 之间线性插值,每隔一段时间调一次 `spectrum_anim_cb(obj, v)`,`v` 是当前帧号。帧率约 30fps。

### 三个函数的协作(数据驱动绘制)

```
spectrum_anim_cb (动画每帧调用, ~30fps)   ← 指挥: 推进数据
   │
   ├─ 改 spectrum_i = v          ← 时间推进
   ├─ 改 bar_ofs / bar_rot       ← 低音驱动的偏移
   ├─ lv_image_set_scale(封面)   ← 低音驱动的封面缩放
   └─ lv_obj_invalidate(obj)     ← 标记脏
        │
        ▼ 下个刷新周期(33ms)
   LVGL 重绘 spectrum_obj + album_image_obj
        │
        ├─ spectrum_draw_event_cb        ← 画师: 读数据画图
        │    读 spectrum[spectrum_i] + bar_ofs + bar_rot
        │    → 画20根频谱柱(新位置+新高度+新颜色)
        │
        └─ image 默认绘制
             按 scale 画封面(新缩放)
        │
        ▼
   屏幕呈现: 频谱柱跳动 + 封面脉动
```

---

## 五、关键设计要点

### 1. 暂停 = 删动画

`lv_demo_music_pause` 里:
```c
spectrum_i_pause = spectrum_i;                         // 记住停在哪
lv_anim_delete(spectrum_obj, spectrum_anim_cb);        // ★删动画 → 不再调本函数
lv_obj_invalidate(spectrum_obj);                       // 重绘一次(静止)
```
动画一删,`spectrum_anim_cb` 不再被调用 → `spectrum_i` 不再推进 → 频谱定格静止。**频谱的"动"完全靠这个动画回调驱动。**

### 2. 播完自动切歌

```c
static void spectrum_end_cb(lv_anim_t * a) {
    lv_demo_music_album_next(true);   // 动画到最后一帧 → 自动下一首
}
```

### 3. 切歌时多动画并行

`track_load` 启动多个动画,各有自己的 exec_cb,互不冲突(改不同属性):

| exec_cb | 作用 | 时长 | 何时 |
|---------|------|------|------|
| `album_fade_anim_cb` | 旧封面淡出 | 500ms | 切歌 |
| `_obj_set_x_anim_cb` | 旧封面滑走 | 500ms | 切歌 |
| `_image_set_scale_anim_cb` | 旧封面缩小 | 500ms | 切歌 |
| `_image_set_scale_anim_cb` | 新封面放大 | 500ms+100ms延迟 | 切歌 |
| `album_fade_anim_cb` | 新封面淡入 | 500ms+100ms延迟 | 切歌 |
| `spectrum_anim_cb` | 频谱推进 | 整首歌 | 播放期间持续 |

切歌瞬间可能 5-6 个动画并行,每帧各调一次 exec_cb,各改各的属性(opa/x/scale/spectrum_i),LVGL 下一轮刷新统一画出来。

---

## 六、整体机制图

```
lv_timer_handler()  每帧
   │
   ├─ anim timer: 遍历所有活跃动画
   │    ├─ 动画A: 算当前值 → exec_cbA(obj, vA)  → 改属性A
   │    ├─ 动画B: 算当前值 → exec_cbB(obj, vB)  → 改属性B
   │    └─ 动画C: 算当前值 → exec_cbC(obj, vC)  → 改属性C
   │        (各自独立, 按自己时长/进度)
   │
   └─ refr timer: 重绘脏区域
        └─ 把上面改过的属性统一画到屏幕
```

---

## 七、一句话总结

> 每个 `lv_anim_start` 创建独立动画,动画管理器每帧遍历所有**仍在运行**的动画,按各自进度调各自 `exec_cb`。`exec_cb` 多为单行类型适配器(anim 签名固定 `void(*)(void*,int32_t)`),`spectrum_anim_cb` 是特例——它是"指挥非画师",推进 `spectrum_i` + `invalidate` + 低音驱动偏移/封面缩放,自己不画图,画图靠 `spectrum_draw_event_cb` 响应重绘。动画帧(推进数据)和刷新帧(画图)是两个独立 job,都由 `lv_timer_handler` 驱动。

---

## 附: 相关源码位置

- `lv_anim_t` 结构 / `lv_anim_start` / `lv_anim_set_*` — `lvgl/src/misc/lv_anim.c` / `lv_anim.h`
- `spectrum_anim_cb` — `lvgl/demos/music/lv_demo_music_main.c:911`
- 适配器回调 `_obj_set_x_anim_cb` / `_image_set_scale_anim_cb` / `album_fade_anim_cb` — 同文件
- 动画注册(`resume`) — 同文件 `lv_demo_music_resume` (359行)
- `spectrum_end_cb` — 同文件 (自动切歌)
