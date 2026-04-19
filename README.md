# Gomoku-Project

同济大学软件学院程序设计范式课程五子棋项目（2025 届赛评第 13 名）。

本项目已演进为双运行模式：

- 原生模式：直接编译 `src/main.c` 为命令行程序，按照文本协议进行对局。
- Web 模式：将 `src/main.c` 编译为 WebAssembly，由前端页面直接调用 C 引擎。

## 1. 项目目标

实现一个可复用的五子棋 AI 引擎，并在两种场景下运行：

- 本地原生命令行对弈（可用于评测、联调、对拍）。
- 浏览器图形界面交互（可视化、易展示）。

核心原则是单一算法内核：搜索逻辑、评估逻辑、候选生成都集中在 C 侧，前端只承担交互和渲染。

## 2. 算法与引擎设计

引擎位于 `src/main.c`，核心技术如下：

- 搜索策略：Minimax + Alpha-Beta 剪枝。
- 搜索深度：默认 `SEARCH_DEPTH = 7`。
- 置换表：基于 Zobrist Hash 的 TT（Transposition Table）。
- 棋型评估：活二/眠二/活三/冲四/活四/连五及跳跃棋型。
- 候选生成：仅在邻近落子区域扩展，并按启发式分数排序后截断（Beam-like 限宽）。

该组合在速度与棋力之间做了工程化平衡，适合课程项目与演示场景。

## 3. 运行模式说明

### 3.1 原生模式（默认编译）

不定义 `GOMOKU_WASM` 宏时，程序启用命令行主循环：

- 入口：`main()`。
- 交互方式：标准输入输出文本协议。
- 开局逻辑：保持最初版本的中心四子初始化。

协议命令：

- `START <aiPlayerId>`：初始化引擎与棋盘，`aiPlayerId` 为 `1` 或 `2`。
- `PLACE <row> <col>`：记录对手落子。
- `TURN`：请求 AI 计算并返回下一手。
- `END`：结束本局。

示例：

```text
START 1
OK
PLACE 4 4
TURN
5 4
END
```

### 3.2 WebAssembly 模式

定义 `GOMOKU_WASM` 宏时，不编译命令行主循环，而导出 wasm 接口：

- 初始化：`gomoku_init(humanPlayerId, seed)`
- 落子同步：`gomoku_set_cell(row, col, piece)`
- 求解：`gomoku_determine_next_play_packed()`
- 判胜：`gomoku_check_win(row, col, player)`
- 其他导出：`gomoku_get_board_copy`、`gomoku_determine_next_play`、`gomoku_get_winning_line`

前端页面在 `src/index.html`，通过 `fetch + WebAssembly.instantiate` 直接调用上述导出函数。

## 4. 目录结构

```text
Gomoku-Project/
├─ src/
│  ├─ main.c            # C 引擎核心（原生 + wasm 双模式）
│  ├─ gomoku.wasm       # wasm 构建产物
│  ├─ index.html        # 前端 UI（React + Tailwind CDN）
│  └─ libs/             # 前端依赖库
├─ tools/
│  └─ run_server.py     # 本地静态服务器（自动开浏览器/CORS/禁缓存）
├─ assets/              # 课程资料与附件
├─ README.md
└─ LICENSE
```

## 5. 构建与运行

以下示例基于 Windows PowerShell，工作目录为项目根目录。

### 5.0 准备环境

在开始编译前，请先确认以下工具可用：

- Python 3：用于启动本地静态服务器，避免浏览器直接打开 HTML 时加载 wasm 失败。
- LLVM clang：用于将 `src/main.c` 编译为原生 `exe`，也用于生成 `wasm`。
- 现代浏览器：用于运行 `src/index.html` 前端页面。

如果你的 LLVM 安装路径不同，请把下面命令中的 `C:\Program Files\LLVM\bin\clang.exe` 替换成实际路径。

### 5.1 构建并运行原生模式

原生模式会把 `src/main.c` 编译成 Windows 可执行文件 `gomoku_native.exe`。该模式用于命令行对局、联调和算法验证，不依赖浏览器，也不依赖 wasm。

编译命令如下：

```powershell
clang -O2 -o src\gomoku_native.exe src\main.c
```

如果你想顺手验证程序是否能正常启动，可以再执行下面的最小对局脚本：

```powershell
@'
START 1
TURN
END
'@ | .\src\gomoku_native.exe
```

命令说明：

- `-O2`：开启常规优化，兼顾编译速度和运行性能。
- `-o src\gomoku_native.exe`：指定输出文件为 `src/gomoku_native.exe`。
- `src\main.c`：源码入口文件。
- 上面的第二段脚本是运行测试，不是编译命令本身。

如果你只想编译，不想立即运行，也可以只执行第一行编译命令。

编译完成后，生成的可执行文件位于 `src/gomoku_native.exe`。

### 5.2 构建 WebAssembly

Web 模式会把 `src/main.c` 编译成 `src/gomoku.wasm`，供浏览器里的 `src/index.html` 直接调用。这个模式要求编译时启用 `GOMOKU_WASM` 宏，并导出前端需要的 C 接口。

编译命令如下：

```powershell
clang --% --target=wasm32 -O3 -DGOMOKU_WASM -nostdlib -Wl,--no-entry -Wl,--export=gomoku_init -Wl,--export=gomoku_get_board_copy -Wl,--export=gomoku_set_cell -Wl,--export=gomoku_determine_next_play -Wl,--export=gomoku_determine_next_play_packed -Wl,--export=gomoku_check_win -Wl,--export=gomoku_get_winning_line -Wl,--export-memory -o src\gomoku.wasm src\main.c
```

命令说明：

- `--target=wasm32`：将目标平台指定为 WebAssembly。
- `-DGOMOKU_WASM`：切换到 wasm 分支，关闭命令行主循环，启用导出函数。
- `-nostdlib`：不链接标准 C 运行时，减小 wasm 体积并避免不必要的依赖。
- `-Wl,--no-entry`：告诉链接器这是一个没有 `main()` 入口的 wasm 模块。
- `-Wl,--export=...`：把前端需要调用的函数导出给 JavaScript。
- `-Wl,--export-memory`：导出线性内存，供前端读写棋盘状态。

编译完成后，生成的 wasm 文件位于 `src/gomoku.wasm`，并且必须和 `src/index.html` 放在同一个静态服务目录下，前端才能正常通过 `fetch` 加载。

如果你修改了 `src/main.c`，请重新生成 `src/gomoku.wasm`，否则浏览器里看到的仍然是旧逻辑。

### 5.3 启动前端页面

请使用仓库内置脚本 `tools/run_server.py` 启动本地 HTTP 服务，避免直接双击文件导致 wasm 加载失败。

默认启动（服务目录为 `src/`，端口从 8000 开始自动探测）：

```powershell
python .\tools\run_server.py
```

安全模式（仅本机访问，不暴露局域网）：

```powershell
python .\tools\run_server.py --local
```

自定义目录与端口：

```powershell
python .\tools\run_server.py --dir .\src --port 9000
```

启动后脚本会自动打开浏览器，默认访问地址形如：`http://localhost:8000/index.html`。

## 6. 工程实现细节

- 为兼容 wasm，无动态分配依赖：置换表使用静态存储。
- 候选排序使用内建插入排序，避免依赖标准库 `qsort`。
- 原生与 wasm 在 `boardInit` 上按宏分流：
	- 原生：中心四子开局（保持最初行为）。
	- wasm：空棋盘开局（匹配前端交互）。

## 7. 常见问题

### Q1: 浏览器提示 wasm 加载失败

通常是因为页面不是通过 HTTP 服务提供，或 `gomoku.wasm` 与 `index.html` 不在同一服务根目录。

### Q2: 原生编译出现 `sscanf` 弃用告警

这是 Windows CRT 安全告警，不影响功能。若需要可进一步替换为 `sscanf_s` 或统一加安全宏。

### Q3: 为什么同时保留原生和 wasm 两套入口

原生模式便于评测和脚本化测试，wasm 模式便于图形展示和交互，两者共用同一 C 核心算法，减少逻辑分叉。

## 8. 许可证

见 `LICENSE` 文件。
