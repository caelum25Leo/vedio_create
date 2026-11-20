# UI 程序架构

Qt 5.7 Widgets，C++11，工程在 `UI/`。窗口编辑一支片子的 brief，不画视频。入口创建 `QApplication`，显示主窗口，进入事件循环。`main(int argc, char *argv[])` 这个签名保留。工程的 CMake 里仍留着 MDB 链接配置，当前目标不编那些文件，窗口也不包含 MDB 头。

启动默认那份 `brief.md` 是 `films/earth-to-fukushima/brief.md`，这支片子就是例子：四格推镜，地球 → 日本列岛 → 福岛县太平洋岸 → 福岛第一核电站，24 秒、1080x1440、推镜顺序 `earth japan coast fukushima`。开新片走「新建片…」。程序从可执行文件所在目录向上找 `director/AI_GUIDE.md`，用来确定仓库根目录。文件编码 UTF-8。带 `--scan` / `--brief` 时不开窗口，只打报告（`tc.cpp`）。

## 四块

| 块 | 职责 | 不负责 |
| --- | --- | --- |
| 窗口 `BriefWindow` | 中文表单、分镜 JS 的拷入与读取、主角关键帧表、新建、打开、保存、清空 | 标记怎么替换，AI 怎么画，JS 怎么认 |
| 数据包 `BriefData` | 一次输入的全部字段，包括主角关键帧 | 哪个控件、文件第几行 |
| 读写 `BriefMarkdown` | 按编号找到正文并替换，只读写一份 `brief.md` | 窗口长什么样，启动 AI，改导演指南 |
| 扫描 `boardscan.cpp` | 从分镜 JS 里认模式、镜头模块、推镜顺序、画幅帧率、依赖和环境要求，生成报告文字 | 控件、写文件、跑 JS |

保存时窗口把控件收成一份 `BriefData`，读写模块用它改当前 `brief.md`。打开时方向相反。**程序不再改 `director/` 下的任何文件**：做片的 AI 第一步就读 `films/<name>/brief.md`，把字段再印进通用指南只会让上一部片子的内容串进下一部。

## 数据包

编号固定：

`title` `style` `audience` `logline` `lastLine` `place` `change` `cast` `hero` `refs` `drawing` `shots` `action` `result` `seconds` `size` `materials` `storyboard` `motion`

片名、对白、地点、动作这些正文用用户写的语言。

另有主体接续，类型是一组关键帧，不是一段话：

| 列 | 含义 |
| --- | --- |
| `t` | 秒 |
| `x` `y` | 画面坐标，原点在左上，锚点在主角中心 |
| `action` | 这一拍在做什么 |

`action` 字段仍是整段交互说明。关键帧是同一主角在时间上的位置和动作，做片的 AI 拿它核对样帧之间的锚点。

`style` 已经没有控件：窗口交回空串，标记对仍写在 `brief.md` 里，给旧文件留着。编号表 `kIds` 保持 19 项（旧 18 项 + 新加的 `shots`），旧的 `brief.md` 里没有 `shots` 标记也能读回，保存时按 `## Drawing` 后面的位置补一对标题加标记。

`seconds` 存秒数，`storyboard` 存 `yes` 或 `no`。窗口里分别是数字框和勾选框。`refs` 存图片的仓库相对路径，不存图片二进制。`drawing` 存主 JS 的仓库相对路径，`shots` 存推镜顺序（一行镜头 id，空格隔开），`size` 存画幅 —— A/B/C 从 JS 读出来，D 模式的 JS 里根本没有这个数，所以由人填，留空表示以 JS 自己的画布为准，`materials` 存「读取 JS」生成的报告。四者都不把脚本正文写进 brief。

## 窗口

启动时定位到一份 `brief.md`，文件存在就把里面的内容读回输入框 —— 示例不写死在控件里，而是写在一片真正的 `films/<name>/brief.md` 上（默认那份就是例片 `earth-to-fukushima`），这样打开自己的片子时不会被示例盖住。控件上只留 placeholder 提示例子的写法（片长默认 24 秒）。切到别的片子走底部那一行：「片子」下拉（见下）或「打开」。状态栏和「存放位置」一行显示当前路径。

- 分镜 JS：存放位置、主 JS 下拉框、推镜顺序（一行镜头 id，落到 `brief:shots`）、读出的结果、分镜说明与依赖（报告写进这里，落到 `brief:materials`）。按钮为「选择 JS 文件…」（可多选，按选中共有父目录保留相对位置）、「选择整个目录…」（跳过 `node_modules`、`out`、`build`、`dist` 和点文件）、「读取 JS」。选完自动读一次；读只读，不顺手生成预览也不弹浏览器（预览是底部那个按钮自己的事，免得选完文件被抢走注意力）
- 预览（底部按钮，`previewdialog.h/.cpp`）：开一个 `QDialog`，左边把 `films/<片名>/storyboard/` 下的**每个 JS 列成一个按钮**（按钮名 = 文件名去掉扩展名），点下去右边显示那个 JS 渲出来的**静帧**，下面一行是尺寸和像素统计；底部三个按钮是「刷新」、「在浏览器打开这一格」、「关闭」。图由 `QProcess` 跑 `node tools/preview/build-preview.mjs <片子目录> --stills`（`findNode()` 先看 PATH，再试两个默认安装位置）在无头 msedge 里跑真正的垫片和真正的模块得到，落在 `out/preview/<文件名>.png`；打包器给每个 JS 都留一份同名 `.txt`，画不出来的写原因 —— 没有图时对话框贴那句原因，不给空白。图不存在且 `.txt` 也不存在才起 node（`showStill(file, buildIfMissing)`，渲完收尾那次传 `false`，否则渲不出图会无限重起）；渲完仍非零退出，先把能看的图显示出来再把退出码和打包器最后几行写进说明行。「在浏览器打开这一格」开 `preview.html?view=mini&shot=<文件名>`，网页不嵌进 Qt 窗口（Qt 5.7 的 WebEngine 是 Chromium 45，见 `TECHNIQUE.md` §2.1）。「刷新」重扫 `storyboard/`（新拷进来的 JS 会多一个按钮）并重渲全部静帧，顺手也重打了 `preview.html`。`onSaveAs()` 那个槽还在，只是按钮位置让给了「预览」
- 故事：片名、讲给谁、一句话、结尾停住那句、片长、画幅（可编辑，placeholder 写了例值；留空 = 以 JS 自己的画布为准）、是否先看分镜
- 主体接续：关键帧表。按钮为添加一行、删除当前行
- 可选：分镜说明（地点、变化、登场物、主角、动作、结果、参考图）。这是个可折叠组，默认收起；打开旧的 `brief.md` 时若这些格子有内容会自动展开

画布尺寸和帧率由 `scanBoard()`（`boardscan.cpp`）从主 JS 里正则找：`SIZE = [w, h]`、`canvas.width/height`、`w:…, h:…`、`1920x1080` 这类写法，取第一组在 100–8192 范围内的。模式按有没有带 `u` 的逐镜函数 / `window.STORY`（A）、只有 `renderAt(t)`（B）、只列成品图或没有进度参数（C）推定；当目录里有 ≥ 2 个 `export default { id, name, draw }` 的镜头模块、至少一个自带 `MARKS + frameFor()`、而 `draw()` 里又没有任何进度参数时判为 **D**（一格一个模块，镜与镜之间靠插值相机推进）。主 JS 靠「被兄弟模块 import 最多」选出（例片里是投影底本 `earth.js`），D 模式再按 MARKS 的经纬跨度从大到小排一遍推镜顺序，无 MARKS 的底本排第一。报告里除了模式还写：推镜怎么接（radius 取对数插值、锚点像素线性走、反解 originX/originY）、取景锚点、哪些模块要先给 `wx` 打桩、模块内部位图边长上限、画幅那一栏为什么要人填。相对 import 和被引用的图片、字体逐个解析到 `storyboard/` 下（`./` 和 `../` 前缀按字符数剔掉），找不到的列成「缺这些」。全部是启发式，给人看的，不是判决。

底部一行左边是**「片子」下拉**：`rebuildFilmList()` 列 `films/` 下的**每一个目录**（按名字排序，不看里面有没有 brief.md），显示 `目录名 —— 片名`，itemData 存这个目录的 `brief.md` 路径，选中就是 `onFilmChanged()`。目录里还没有 brief.md 的（手工拷出来的文件夹、旧流程留下的片子）行尾标「—— 还没有 brief」，切过去就是把表单空着摊开、片名填成目录名，存一次就生成那份 brief.md；已经有 brief.md 的就读回来整份换上去。当前这份在 `films/` 以外（「打开」选了别的地点的文件）会多列一行。切之前 `confirmLeave()` 拿 `collect()` 和上次读回的快照 `m_loaded` 比一比，改过就先问存 / 丢 / 取消；`m_loaded` 在 `showData`、`saveCurrent`、`onClear` 三处刷新，刚打开文件不会误报改过。同一个守卫也接在「新建片…」和「打开」上。下拉右边是原来的五个按钮：

| 按钮 | 作用 |
| --- | --- |
| 新建片… | 在 `films/` 下建目录（名字限 `[a-z0-9-]`，大写和中文不给建 —— 要这种名字就在资源管理器里建文件夹，下拉会列出它），清空表单并把路径指到 `films/<名字>/brief.md`，立刻进下拉 |
| 打开 | 选择一份 `brief.md`（默认落在 `films/`），把标记读回输入框，并把当前路径换成这份文件；与下拉是同一条加载路径，只是多一层文件对话框 |
| 保存 | 把当前输入写入状态栏上的那份 `brief.md`。只写这一个文件 |
| 预览 | 开「预览」对话框（见上）：`storyboard/` 一个 JS 一个按钮，点下去看那张静帧 |
| 清空 | 清掉输入框、参考图和关键帧表。不改已经写在磁盘上的文件 |

## 文件里怎么定位

人看标题。程序只认标记，不搜标题文字。

```markdown
## Logline

<!-- brief:logline -->
一条鱼，从海沟最底游向海面。
<!-- /brief:logline -->

## Motion

<!-- brief:motion -->
t x y action
0.0 1180 1180 从海沟底部出发
18.0 960 268 一路向上，逃出生天
<!-- /brief:motion -->
```

正文从开标记的下一行到闭标记的上一行。保存 `brief.md` 时只替换这一段。编号还没有标记时，按固定顺序补到文件末尾；`drawing`、`shots` 这两个新编号例外，它们带着自己的标题（`## Drawing`、`## Shot order`）插到 `## Motion` 前面。空字段也保留一对标记。正文里若出现 `<!-- /brief:`，拒绝写入。`brief.md` 的第一行 `# ` 标题会改成片名。

`brief:motion` 的第一行是表头 `t x y action`，后面每行一拍。前三列是时间、横位置、纵位置，这一行剩下的文字是动作。`brief:shots` 只有一行镜头 id（`earth japan coast fukushima`）；D 模式的模块目录里没有登记表，index.js 里的顺序也不是故事顺序，所以这个顺序由窗口承载。

## 写哪些文件

| 路径 | 谁写 | 内容 |
| --- | --- | --- |
| `films/<name>/brief.md` | 保存 | 全部字段，含 `brief:motion` 和 `brief:materials` 的报告 |
| `films/<name>/assets/` | 选择图片 | 复制进来的参考图 |
| `films/<name>/storyboard/` | 选择 JS 文件… / 选择整个目录… | 用户的分镜 JS 和它的全部依赖。主 JS 的相对路径写在 `brief:drawing` |
| `director/DIRECTOR.md`、`director/TECHNIQUE.md` | **不写** | 通用模板，字段不再注入。文件里留着成对的空标记，只是给旧流程兼容 |
| `director/styles/` | 不写 | 新流程不读它，窗口也不再列风格 |

## 做视频

窗口不启动 AI，也不调用 `renderAt`。保存完 `brief.md`，由人把片子目录交给按 `director/AI_GUIDE.md` 干活的 AI。
