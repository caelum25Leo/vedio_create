# UI 工程文件

| 文件 | 干什么 |
| --- | --- |
| `CMakeLists.txt` | 用 Qt 5.7 Widgets 编出 `testCode`。源文件是入口、窗口、预览对话框、读写、分镜扫描和风格清单。MDB 的链接配置留在这里，当前目标不编那些缺失的 MDB 示例。源文件按 UTF-8 编译。 |
| `tc.cpp` | 程序入口。创建 `QApplication`，显示主窗口，进入事件循环。`main` 保持 `int main(int argc, char *argv[])`。带 `--scan <目录或 JS>` 不开窗口打「读取 JS」的报告，带 `--brief <brief.md>` 打这份 brief 读回来的关键字段，给做片的 AI 自查文件能不能读进去。 |
| `tc.h` | 旧的函数声明，窗口不用它。 |
| `briefdata.h` | 数据包。一次编辑的全部字符串字段（含主 JS `drawing`、推镜顺序 `shots`），加上主角关键帧 `t`、`x`、`y`、`action`。 |
| `boardscan.h` | 分镜 JS 扫描器的接口：`ShotModule`（一格镜头模块认出来的标志位）、`BoardScan`（模式、画幅、镜头表、依赖、注意事项），以及 `jsFilesUnder`、`guessEntry`、`scanBoard`、`describe`。窗口和 `--scan` 共用它。 |
| `boardscan.cpp` | 用正则读分镜 JS：认出 A/B/C/D 四档模式，D 是「一格一个模块、各画一张静态取景、靠镜间插值相机推进」；按 MARKS 跨度从大到小排推镜顺序，投影底本排第一；报取景锚点、`wx` 打桩、模块内部位图上限、缺哪些相对引用。主 JS 靠「被兄弟模块 import 最多」选出来。全部是启发式，写给人看的。 |
| `briefmarkdown.h` | 读写接口。`load` 读进数据包，`save` 写回 `brief.md`。只这两个：往导演说明里注字段的做法已经拆掉了。 |
| `briefmarkdown.cpp` | 按 `<!-- brief:编号 -->` 替换正文，缺标记就补上；`drawing`、`shots` 两块补的时候连标题一起插。只动 `films/<name>/brief.md`，不动 `director/` 下的任何文件。文件末尾留着一段注释说明为什么不再写导演副本。 |
| `briefwindow.h` | 主窗口类。声明分镜 JS 区、故事区、关键帧表、可折叠的可选说明区，以及新建、片子下拉、打开、保存、另存为（槽留着，按钮位置已让给「预览」）、预览、清空、选图、选 JS、读 JS 的槽；成员里有当前 `m_path`、上次读回的快照 `m_loaded`（拿来看改过没改过）。 |
| `briefwindow.cpp` | 中文界面。启动时定位 `brief.md`，默认那份就是例片 `films/earth-to-fukushima/brief.md`（四格推镜：地球 → 日本 → 海岸线 → 福岛核电站），文件存在就读回，新控件的默认值也按这支片子摆。「选择 JS 文件…」「选择整个目录…」把用户的 JS 连同依赖拷到 `films/<name>/storyboard/`，路径记 `brief:drawing`；「读取 JS」调 `scanBoard()` 推定 A/B/C/D 模式、画幅、帧率、镜头表、推镜顺序和缺失依赖，报告写进 `brief:materials`，画幅和推镜顺序只在格子里空着时回填，不覆盖用户改过的。画幅是可编辑的（D 模式的 JS 里没有画幅常量），另有「推镜顺序」一行。选图复制到片子的 `assets/`。底部一行左边是「片子」下拉：列 `films/` 下每一个目录（不管里面有没有 brief.md），选中就切过去（`rebuildFilmList` / `onFilmChanged`）；还没有 brief.md 的行尾标「还没有 brief」，切过去等于开新一支片子，存一次就生成那份。切走前若表单改过会先问存 / 丢 / 取消（`confirmLeave`，同一个守卫也接在「新建片…」和「打开」上）。不启动 AI，不做画面渲染，也不改 `director/`。 |
| `previewdialog.h` | 「预览」对话框类。入参是仓库根 + 片子目录；对外只有构造函数，其余全是私有槽和私有 helper。 |
| `previewdialog.cpp` | 列 `storyboard/` 下每个 JS 为一个按钮，点下去显示 `out/preview/<文件名>.png` 那张静帧和同名 `.txt` 的说明；没图就报原因，图和原因都没有才起 `node tools/preview/build-preview.mjs <片子目录> --stills`（收尾那次不再起，免得死循环）。「刷新」重扫目录并重渲全部，「在浏览器打开这一格」开 `preview.html?view=mini&shot=<文件名>`，「关闭」只关这个窗口。 |
| `stylecatalog.h` | 风格条目：文件夹名、英文名、中文名。当前窗口已不再调用（画面由用户的 JS 定）。 |
| `stylecatalog.cpp` | 99 种风格的中英对照，来自 `director/styles/README.md`。仍在编译里，给旧分支留着。 |
| `ARCHITECTURE.md` | 窗口、数据包、读写怎么拆，JS 怎么拷怎么读，以及每个路径由谁写入。 |
| `.vscode/settings.json` | 编辑器里打开这个工程时的 CMake 路径和构建目录。 |
| `.vscode/launch.json` | 调试 `build/Debug/testCode.exe` 或 Release 的启动配置。 |
