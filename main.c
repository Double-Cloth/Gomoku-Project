// --- 头文件 --- //
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --- 宏定义与常量 --- //

// 类型定义
typedef long long LL; // 用于存储棋局评估分数 (需要大范围以区分胜负和细微优势)
typedef unsigned long long ULL; // 用于 Zobrist 哈希键，64位以保证低碰撞率

// 棋盘定义
#define BOARD_SIZE 12 // 棋盘尺寸 (12x12)
#define EMPTY_SLOT 0  // 棋盘空点
#define PIECE_B    1  // 黑棋
#define PIECE_W    2  // 白棋

// 评估分数常量
// 定义极大/极小值, 用于Alpha-Beta剪枝的边界
const LL SCORE_MAX = 8223372036854775808LL;
const LL SCORE_MIN = -8223372036854775808LL;

// 棋型基础分值 (用于 AIFitting)
#define SCORE_FIVE            1111111111LL // 连五 (绝对胜利)
#define SCORE_FOUR_OPEN       1100000LL    // 活四 (下一手必胜)
#define SCORE_THREE_OPEN      1100LL       // 活三 (重要的攻击手段)
#define SCORE_FOUR_RUSH       1000LL       // 冲四 (眠四)
#define SCORE_JUMP_FOUR_OPEN  1000LL       // 跳活四
#define SCORE_JUMP_THREE_OPEN 900LL        // 跳活三
#define SCORE_JUMP_FOUR_SLEEP 800LL        // 跳眠四
#define SCORE_TWO_OPEN        100LL        // 活二
#define SCORE_THREE_SLEEP     100LL        // 眠三
#define SCORE_TWO_SLEEP       10LL         // 眠二
#define SCORE_INVALID         0LL          // 无效

// 对手棋型分数的加权乘数
#define PATTERN_WEIGHT 1 // 这个数值不要低于 0.883 (否则双三判断可能失效)

// 棋型定义
typedef enum {
    PATTERN_INVALID = 0, // 0. 无效或无威胁的棋型
    PATTERN_TWO_SLEEP, // 1. 眠二
    PATTERN_TWO_OPEN, // 2. 活二
    PATTERN_THREE_SLEEP, // 3. 眠三
    PATTERN_THREE_OPEN, // 4. 活三
    PATTERN_FOUR_RUSH, // 5. 冲四 (眠四)
    PATTERN_FOUR_OPEN, // 6. 活四
    PATTERN_FIVE, // 7. 连五
    PATTERN_JUMP_THREE_OPEN, // 8. 跳活三 (例如 O_OO)
    PATTERN_JUMP_FOUR_SLEEP, // 9. 跳眠四 (例如 XOO_OO)
    PATTERN_JUMP_FOUR_OPEN, // 10. 跳活四 (例如 _OO_OO_)
    PATTERN_COUNT // 11. 棋型总数 (方便遍历计算对手分数)
} PatternType;

// 方向向量 (row, col) - 用于线性评估
// 只需要检查4个方向即可覆盖所有8个方向 (水平, 垂直, 左上到右下, 右上到左下)
const int gDirectionRow[] = {1, 0, 1, 1}; // 行变化 (垂直, 水平, \ , /)
const int gDirectionCol[] = {0, 1, 1, -1}; // 列变化 (垂直, 水平, \ , /)

// Alpha-Beta 搜索的最大深度 (奇数层确保AI多下一步)
#define SEARCH_DEPTH 7

// 候选着法
#define MAX_CANDIDATES (BOARD_SIZE * BOARD_SIZE) // 候选着法数组的最大容量

// 置换表
#define TT_SIZE (1 << 20) // 置换表大小 (2^20, 约一百万条目)
#define TT_TYPE_EXACT 0   // 分数类型: 精确值 (Alpha 和 Beta 之间)
#define TT_TYPE_ALPHA 1   // 分数类型: Alpha (下界, 实际分数 >= score, 发生了 Beta 剪枝)
#define TT_TYPE_BETA  2   // 分数类型: Beta (上界, 实际分数 <= score, 发生了 Alpha 剪枝)

// --- 核心数据结构 --- //

/**
 * @brief 置换表 (Transposition Table) 条目
 * 用于存储已搜索过的棋局状态, 避免重复计算
 */
typedef struct {
    ULL key; // Zobrist 键 (用于快速校验是否是同一个棋局)
    LL score; // 评估分数
    int depth; // 剩余搜索深度 (存储时该局面的剩余深度)
    int type; // 分数类型 (EXACT, ALPHA, BETA)
} TT_Entry;

/**
 * @brief 棋型得分表 (区分我方和对手)
 */
typedef struct {
    LL AIFitting[PATTERN_COUNT]; // 我方(AI)下出不同棋型时的得分
    LL OppFitting[PATTERN_COUNT]; // 对手下出不同棋型时的得分
} PatternTable;

/**
 * @brief 坐标与评估
 * 用于存储一个着法(坐标)及其启发式评估分
 */
typedef struct {
    int row; // 行
    int col; // 列
    LL score; // 该位置的启发式评估分 (用于着法排序)
} Coord;

/**
 * @brief 存储棋局评估函数在单一方向上搜索棋型的结果
 */
typedef struct {
    int consecutiveCount; // 连续棋子数 (不含中心点)
    int openEnd; // 连续棋子后是否为空 (1=是, 0=否)
    int jumpCount; // 跳跃棋子数
    int jumpOpen; // 跳跃棋子后是否为空 (1=是, 0=否)
    int jumpBlocked; // 跳跃棋子后是否被阻挡 (1=是, 0=否)
} LineSearchResult;

/**
 * @brief 候选着法列表
 */
typedef struct {
    Coord candidates[MAX_CANDIDATES]; // 候选着法数组
    int count; // 候选着法数量
} CandidateList;

/**
 * @brief 棋盘状态
 */
typedef struct {
    ULL currentHash; // 当前棋盘的 Zobrist 哈希值
    int layout[BOARD_SIZE][BOARD_SIZE]; // 棋盘布局 (0:空, 1:B, 2:W)
} ChessBoard;

// --- 全局变量 --- //

// 玩家ID (由 "START" 命令设置)
int gAiPlayerId; // AI 使用的棋子
int gOppPlayerId; // 对手使用的棋子

// PRNG状态 (用于 Xorshift64* 随机数生成)
static ULL gPrngState;
// Zobrist 哈希表 (3种棋子状态[空,B,W], 棋盘尺寸)
// gZobristKeys[p][i][j] 表示棋子p在(i,j)位置时的随机哈希值
ULL gZobristKeys[3][BOARD_SIZE][BOARD_SIZE];
// 全局置换表 (TT)
TT_Entry *gTranspositionTable;

// 这是AI评估的核心: 不同棋型的基础分值
PatternTable gPatternScores;

// 全局唯一棋盘状态
ChessBoard gCurrentBoard;

// --- 随机数生成函数 --- //

/**
 * @brief 为自定义 PRNG (Xorshift64*) 播种
 * @param seed 种子。
 */
void seedRand(ULL seed) {
    // 步骤 1: 检查种子是否为 0
    if (seed == 0) {
        // 步骤 2: 如果种子为 0, 使用一个非零的魔术数作为后备
        // (xorshift 算法在状态为0时会卡住)
        seed = 0xBADF00DDEADBEEFULL; // 一个任意的非零常量
    }
    // 步骤 3: 设置全局 PRNG 状态
    gPrngState = seed;
}

/**
 * @brief 生成一个高质量的 64 位无符号随机整数 (xorshift64*)
 * 用于 Zobrist 哈希键的生成
 * @return 随机 HashKey
 */
ULL genU64Rand() {
    // 这是 xorshift64* 算法的标准实现
    // 步骤 1: 执行三次位移和异或操作 (xorshift)
    gPrngState ^= gPrngState >> 12; // a
    gPrngState ^= gPrngState << 25; // b
    gPrngState ^= gPrngState >> 27; // c

    // 步骤 2: 将结果乘以一个魔术常数 (使其成为 xorshift*)
    // 这步操作能极大提高随机数的质量和周期
    return gPrngState * 0x2545F4914F6CDD1DULL;
}

// --- Zobrist 与置换表函数 --- //

/**
 * @brief 初始化 Zobrist 哈希键表和置换表
 */
void ttInit() {
    // 步骤 1: 使用当前时间为随机数生成器播种
    seedRand((ULL) time(NULL));

    // 步骤 2: 遍历所有棋子状态 (0=空, 1=黑, 2=白)
    for (int p = 0; p < 3; p++) {
        // 步骤 3: 遍历棋盘所有行
        for (int i = 0; i < BOARD_SIZE; i++) {
            // 步骤 4: 遍历棋盘所有列
            for (int j = 0; j < BOARD_SIZE; j++) {
                // 步骤 5: 为 [状态][行][列] 的组合分配一个唯一的随机64位数
                gZobristKeys[p][i][j] = genU64Rand();
            }
        }
    }

    // 步骤 6: 为全局置换表分配内存
    gTranspositionTable = (TT_Entry *) malloc(sizeof(TT_Entry) * TT_SIZE);

    // 步骤 7: 检查内存分配是否成功
    if (gTranspositionTable == NULL) {
        // 步骤 8: 如果失败, 打印错误并退出
        fprintf(stderr, "Failed to allocate memory for TT.\n");
        exit(1);
    }

    // 步骤 9: 将置换表内存清零
    // 确保所有条目的 key, depth, score 初始都为 0, 表示 "空"
    memset(gTranspositionTable, 0, sizeof(TT_Entry) * TT_SIZE);
}

/**
 * @brief 从置换表查询
 * @param key 当前 Zobrist 哈希
 * @param depth 当前搜索深度 (剩余深度)
 * @param alpha 当前 Alpha 值
 * @param beta 当前 Beta 值
 * @return 查找到的分数，如果未命中或深度不足则返回 SCORE_MIN - 1
 */
LL ttSearch(const ULL key, const int depth, const LL alpha, const LL beta) {
    // 步骤 1: 计算哈希键在表中的索引 (使用取模)
    const TT_Entry *entry = &gTranspositionTable[key % TT_SIZE];

    // 步骤 2: 检查 Zobrist 键是否匹配 (防止哈希碰撞)
    // 并检查存储的深度是否 >= 当前深度 (存储的结果是否足够好)
    if (entry->key == key && entry->depth >= depth) {
        // 步骤 3: 命中，根据存储的类型返回分数

        // 类型 3a: 精确值 (TT_TYPE_EXACT)
        // 存储的分数是 [alpha, beta] 范围内的精确值
        if (entry->type == TT_TYPE_EXACT)
            return entry->score;

        // 类型 3b: Alpha 值 (下界, TT_TYPE_ALPHA)
        // 存储的分数是 "至少" (>=) entry->score, 且它导致了 Alpha 剪枝
        // 如果存储的下界 (entry->score) 已经小于等于我们当前的 alpha, 它仍然有用
        if (entry->type == TT_TYPE_ALPHA && entry->score <= alpha)
            return alpha;

        // 类型 3c: Beta 值 (上界, TT_TYPE_BETA)
        // 存储的分数是 "至多" (<=) entry->score, 且它导致了 Beta 剪枝
        // 如果存储的上界 (entry->score) 已经大于等于我们当前的 beta, 它仍然有用
        if (entry->type == TT_TYPE_BETA && entry->score >= beta)
            return beta;
    }

    // 步骤 4: 未命中或深度不足, 返回一个特殊值表示 "没找到"
    return SCORE_MIN - 1LL;
}

/**
 * @brief 存储到置换表
 * @param key Zobrist 哈希
 * @param depth 搜索深度 (剩余深度)
 * @param score 评估分数
 * @param type 条目类型 (EXACT, ALPHA, BETA)
 */
void ttStore(const ULL key, const int depth, const LL score, const int type) {
    // 步骤 1: 计算哈希键在表中的索引
    TT_Entry *entry = &gTranspositionTable[key % TT_SIZE];

    // 步骤 2: 替换策略 (深度优先)
    // 仅当新条目的深度 >= 旧条目时才覆盖
    // (来自更深搜索的结果通常更准确)
    if (entry->depth <= depth) {
        // 步骤 3: 存储所有信息
        entry->key = key; // 存储 Zobrist 键 (用于碰撞检测)
        entry->depth = depth; // 存储搜索深度
        entry->score = score; // 存储评估分
        entry->type = type; // 存储分数类型
    }
}

// --- 棋盘状态管理 --- //

/**
 * @brief 初始化棋盘 (设置开局棋子并计算初始哈希)
 * @param board 指向要初始化的棋盘
 */
void boardInit(ChessBoard *board) {
    // 步骤 1: 将棋盘布局全部清零 (设为空)
    memset(board->layout, 0, sizeof(board->layout));

    // 步骤 2: 计算棋盘中心点 (处理奇偶尺寸)
    const int centerA = (BOARD_SIZE + 1) / 2 - 1;
    const int centerB = BOARD_SIZE / 2;

    // 步骤 3: 在棋盘中心放置4颗初始棋子
    board->layout[centerA][centerA] = PIECE_W; // (5, 5) = 白
    board->layout[centerB][centerB] = PIECE_W; // (6, 6) = 白
    board->layout[centerB][centerA] = PIECE_B; // (6, 5) = 黑
    board->layout[centerA][centerB] = PIECE_B; // (5, 6) = 黑

    // 步骤 4: 计算初始 Zobrist 哈希
    board->currentHash = 0; // 初始哈希为 0
    // 步骤 5: 遍历整个棋盘
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            // 步骤 6: 如果该点不是空的
            if (board->layout[i][j] != EMPTY_SLOT) {
                // 步骤 7: 将该棋子在该位置的 Zobrist 键 异或(^) 到总哈希中
                board->currentHash ^= gZobristKeys[board->layout[i][j]][i][j];
            }
        }
    }
}

/**
 * @brief 更新棋盘 (用于落子或悔棋)，增量更新 Zobrist 哈希
 * @param board 指向要更新的棋盘
 * @param row 行
 * @param col 列
 * @param piece 棋子 (EMPTY_SLOT, PIECE_B, PIECE_W)
 */
void boardUpdate(ChessBoard *board, const int row, const int col, const int piece) {
    // Zobrist 哈希的增量更新

    // 步骤 1: "移除" (异或掉) (row, col) 位置上 *旧* 棋子状态的哈希值
    board->currentHash ^= gZobristKeys[board->layout[row][col]][row][col];

    // 步骤 2: "添加" (异或上) (row, col) 位置上 *新* 棋子状态的哈希值
    board->currentHash ^= gZobristKeys[piece][row][col];

    // 步骤 3: 实际更新棋盘数组
    board->layout[row][col] = piece;
}

// --- 棋局评估函数 --- //

/**
 * @brief 加载棋型得分 (初始化 gPatternScores)
 */
void loadPatternScores() {
    // 步骤 1: 确保全局表在初始化前是干净的
    memset(&gPatternScores, 0, sizeof(PatternTable));

    // 步骤 2: 初始化 AI (我方) 的棋型得分 (使用宏定义的分数)
    gPatternScores.AIFitting[PATTERN_FIVE] = SCORE_FIVE;
    gPatternScores.AIFitting[PATTERN_FOUR_OPEN] = SCORE_FOUR_OPEN;
    gPatternScores.AIFitting[PATTERN_THREE_OPEN] = SCORE_THREE_OPEN;
    gPatternScores.AIFitting[PATTERN_FOUR_RUSH] = SCORE_FOUR_RUSH;
    gPatternScores.AIFitting[PATTERN_JUMP_FOUR_OPEN] = SCORE_JUMP_FOUR_OPEN;
    gPatternScores.AIFitting[PATTERN_JUMP_THREE_OPEN] = SCORE_JUMP_THREE_OPEN;
    gPatternScores.AIFitting[PATTERN_JUMP_FOUR_SLEEP] = SCORE_JUMP_FOUR_SLEEP;
    gPatternScores.AIFitting[PATTERN_TWO_OPEN] = SCORE_TWO_OPEN;
    gPatternScores.AIFitting[PATTERN_THREE_SLEEP] = SCORE_THREE_SLEEP;
    gPatternScores.AIFitting[PATTERN_TWO_SLEEP] = SCORE_TWO_SLEEP;
    gPatternScores.AIFitting[PATTERN_INVALID] = SCORE_INVALID;

    // 步骤 3: 动态计算对手的棋型分数
    for (int i = 0; i < PATTERN_COUNT; i++) {
        // 计算对手的棋型分数 (将 AI 的分数乘以一个权重)
        gPatternScores.OppFitting[i] = gPatternScores.AIFitting[i] * PATTERN_WEIGHT;
    }
}

/**
 * @brief 评估在单一方向上的棋子布局 (analyzeLine 的辅助函数)
 * @param board (只读) 棋盘状态
 * @param pos 评估的中心点 (不搜索此点, 而是从此点的 *下一个* 点开始)
 * @param dRow 行方向向量 (例如 1, 0, -1)
 * @param dCol 列方向向量 (例如 1, 0, -1)
 * @param player 评估的玩家
 * @return LineSearchResult 包含该方向搜索结果的结构体
 */
LineSearchResult searchDirection(const ChessBoard *board, const Coord pos, const int dRow, const int dCol, const int player) {
    // 步骤 1: 初始化所有返回值为 0
    LineSearchResult result = {0, 0, 0, 0, 0};
    const int oppPlayer = player == 1 ? 2 : 1;

    // 步骤 2: 确定起始搜索位置 (中心点的下一个)
    int checkRow = pos.row + dRow;
    int checkCol = pos.col + dCol;

    int foundGap = 0; // 是否找到了一个空档
    int isJumping = 0; // 是否正在跳跃

    // 步骤 3: 循环搜索, 直到出界
    while (checkRow >= 0 && checkRow < BOARD_SIZE && checkCol >= 0 && checkCol < BOARD_SIZE) {
        // 步骤 3a: 刚找到空档 (foundGap=1) 且还未开始跳跃 (isJumping=0)
        if (foundGap && !isJumping) {
            if (board->layout[checkRow][checkCol] == player) {
                // 找到了空档后的第一个己方棋子, 开始 "跳跃"
                isJumping = 1;
                result.jumpCount++;
            } else {
                // 空档后不是己方棋子 (是对手或第二个空), 停止跳跃搜索
                break;
            }

            // 步骤 3b: 正在跳跃中 (isJumping=1)
        } else if (isJumping && foundGap) {
            if (board->layout[checkRow][checkCol] == player) {
                // 连续的跳跃棋子
                result.jumpCount++;
            } else if (board->layout[checkRow][checkCol] == oppPlayer) {
                // 跳跃被对手阻挡
                result.jumpBlocked = 1;
                break;
            } else {
                // 跳跃遇到了空
                result.jumpOpen = 1;
                break;
            }

            // 步骤 3c: 尚未找到空档 (标准连续棋子)
        } else {
            if (board->layout[checkRow][checkCol] == EMPTY_SLOT) {
                // 第一次遇到空
                result.openEnd = 1; // 标记此方向为 "开放"
                foundGap = 1; // 标记已找到空档
            } else if (board->layout[checkRow][checkCol] != player) {
                // 被对手阻挡, 停止
                break;
            } else {
                // 连续的己方棋子
                result.consecutiveCount++;
            }
        }

        // 移动到下一个位置
        checkRow += dRow;
        checkCol += dCol;
    }

    // 步骤 4: 返回该方向的搜索结果
    return result;
}

/**
 * @brief 分析单个点在单个方向上的棋型 (核心评估逻辑)
 * @param board (只读) 棋盘状态
 * @param pos 评估的中心点 (假定该点已被 player 占据)
 * @param dRow 行方向向量 (基础方向, 例如 1, 0, 1, 1)
 * @param dCol 列方向向量 (基础方向, 例如 0, 1, 1, -1)
 * @param player 评估的玩家
 * @return 识别到的棋型 (PatternType)
 */
int analyzeLine(const ChessBoard *board, const Coord pos, const int dRow, const int dCol, const int player) {
    // --- 步骤 1: 正向搜索 (dRow, dCol) ---
    const LineSearchResult fwd = searchDirection(board, pos, dRow, dCol, player);

    // --- 步骤 2: 反向搜索 (-dRow, -dCol) ---
    const LineSearchResult bwd = searchDirection(board, pos, -dRow, -dCol, player);

    // --- 步骤 3: 合并结果 ---

    // 连续棋子数 = 正向 + 反向 + 中心点(1)
    const int consecutiveCount = fwd.consecutiveCount + bwd.consecutiveCount + 1;
    // 开放端: 0=无, 1=正向空, 2=反向空, 3=两端空
    const int openEnds = (fwd.openEnd ? 1 : 0) + (bwd.openEnd ? 2 : 0);

    // --- 步骤 4: 评分计算 ---

    // 步骤 4a: 检查单向跳跃棋型 (例如 O_OO 或 OOO_O)
    if (fwd.jumpCount > 0 && bwd.jumpCount == 0) {
        // 只有正向跳跃
        // 总棋子数 = 连续数 + 正向跳跃数
        if (consecutiveCount + fwd.jumpCount == 3 && openEnds == 3 && fwd.jumpOpen) return PATTERN_JUMP_THREE_OPEN; // 跳活三: _O_OO_
        if (consecutiveCount + fwd.jumpCount == 4 && openEnds == 3 && fwd.jumpBlocked) return PATTERN_JUMP_FOUR_SLEEP; // 跳眠四: _O_OOOX
        if (consecutiveCount + fwd.jumpCount == 4 && openEnds == 1 && fwd.jumpOpen) return PATTERN_JUMP_FOUR_SLEEP; // 跳眠四: XO_OOO_
        if (consecutiveCount + fwd.jumpCount == 4 && openEnds == 3 && fwd.jumpOpen) return PATTERN_JUMP_FOUR_OPEN; // 跳活四: _O_OOO_
    } else if (bwd.jumpCount > 0 && fwd.jumpCount == 0) {
        // 只有反向跳跃
        // 总棋子数 = 连续数 + 反向跳跃数
        if (consecutiveCount + bwd.jumpCount == 3 && openEnds == 3 && bwd.jumpOpen) return PATTERN_JUMP_THREE_OPEN; // 跳活三: _OO_O_
        if (consecutiveCount + bwd.jumpCount == 4 && openEnds == 3 && bwd.jumpBlocked) return PATTERN_JUMP_FOUR_SLEEP; // 跳眠四: XOOO_O_
        if (consecutiveCount + bwd.jumpCount == 4 && openEnds == 2 && bwd.jumpOpen) return PATTERN_JUMP_FOUR_SLEEP; // 跳眠四: _OOO_OX
        if (consecutiveCount + bwd.jumpCount == 4 && openEnds == 3 && bwd.jumpOpen) return PATTERN_JUMP_FOUR_OPEN; // 跳活四: _OOO_O_
    } else {
    }
    // (注意: 仍然未处理 O_O_O 这样的双向跳跃, 和原版一致)

    // 步骤 4b: 检查标准连续棋型
    if (consecutiveCount >= 5) {
        return PATTERN_FIVE; // 连五
    }
    if (consecutiveCount == 4) {
        if (openEnds == 3) return PATTERN_FOUR_OPEN; // 活四: _OOOO_
        if (openEnds > 0) return PATTERN_FOUR_RUSH; // 冲四: XOOOO_ 或 _OOOOX
    }
    if (consecutiveCount == 3) {
        if (openEnds == 3) return PATTERN_THREE_OPEN; // 活三: _OOO_
        if (openEnds > 0) return PATTERN_THREE_SLEEP; // 眠三: XOOO_ 或 _OOOX
    }
    if (consecutiveCount == 2) {
        if (openEnds == 3) return PATTERN_TWO_OPEN; // 活二: _OO_
        if (openEnds > 0) return PATTERN_TWO_SLEEP; // 眠二: XOO_ 或 _OOX
    }

    // 步骤 5: 无任何有效棋型
    return PATTERN_INVALID;
}

/**
 * @brief 评估单个玩家在某个点上的威胁 (计算该点在4个方向上的棋型总分)
 * (此函数用于 evaluateBoardScore, 评估 *已存在* 的棋子)
 * @param board (只读) 棋盘状态
 * @param pos 评估的中心点 (必须是该 player 的棋子)
 * @param player 评估的玩家
 * @return 该点的总威胁分数
 */
LL getPlayerThreat(const ChessBoard *board, const Coord pos, const int player) {
    LL totalScore = 0;

    // 步骤 1: 遍历 4 个基本方向
    for (int i = 0; i < 4; i++) {
        // 步骤 2: 分析该点在该方向上的棋型
        const int patternType = analyzeLine(board, pos, gDirectionRow[i], gDirectionCol[i], player);
        // 步骤 3: 累加该棋型的分数
        totalScore += gPatternScores.AIFitting[patternType];
    }

    // 步骤 4: "双三" 或 "三四" 组合的特殊加分
    // 这是一个关键的启发式规则!
    // 1100(活三) + 900(跳活三) = 2000 >= 1500
    // 1000(冲四) + 1100(活三) = 2100 >= 1500
    // 如果总分在 1500 到 100 万之间 (即, 形成多个威胁, 但又不是活四/连五)
    // 就将其分数 "拔高" 到 100 万, 略低于活四 (110万), 表示这是一个非常强的威胁
    if (totalScore >= 1500LL && totalScore < 1000000LL) {
        totalScore = 1000000LL;
    }

    // 步骤 5: 返回总分
    return totalScore;
}

/**
 * @brief 启发式评估：计算在某个点落子后的即时分数 (用于着法排序)
 * (此函数用于 generateCandidates, 评估空点)
 * @param board (只读) 棋盘状态
 * @param pos 评估的落子点 (必须是空点)
 * @return 该点的启发式分数 (我方得分 + 对方得分)
 */
LL getPositionHeuristic(const ChessBoard *board, const Coord pos) {
    LL aiScore = 0; // 假设 AI 落子在此的分数
    LL oppScore = 0; // 假设 对手 落子在此的分数 (用于防守)

    // 步骤 1: 遍历 4 个基本方向
    for (int i = 0; i < 4; i++) {
        // 步骤 2: *假装* AI 在 pos 点落子, 并评估形成的棋型
        const int aiPattern = analyzeLine(board, pos, gDirectionRow[i], gDirectionCol[i], gAiPlayerId);
        // 步骤 3: *假装* 对手 在 pos 点落子, 并评估形成的棋型
        const int oppPattern = analyzeLine(board, pos, gDirectionRow[i], gDirectionCol[i], gOppPlayerId);

        // 步骤 4: 累加 AI 在此落子的分数
        aiScore += gPatternScores.AIFitting[aiPattern];
        // 步骤 5: 累加 对手 在此落子的分数 (防守价值)
        oppScore += gPatternScores.OppFitting[oppPattern];
    }

    // 步骤 6: 同样应用 "双三" / "三四" 威胁加成 (防守)
    if (oppScore >= 1500LL && oppScore < 1000000LL) {
        oppScore = 1000000LL;
    }
    // 步骤 7: 同样应用 "双三" / "三四" 威胁加成 (进攻)
    if (aiScore >= 1500LL && aiScore < 1000000LL) {
        aiScore = 1000000LL;
    }

    // 步骤 8: 返回总启发分 = 进攻分 + 防守分
    // (一个点既能进攻又能防守, 它的价值最高)
    return aiScore + oppScore;
}

/**
 * @brief 评估整个棋盘的静态分数 (用于 Alpha-Beta 搜索的叶节点)
 * @param board (只读) 棋盘状态
 * @return 棋盘总分 (我方总分 - 各种总分)
 */
LL evaluateBoardScore(const ChessBoard *board) {
    LL aiTotal = 0; // AI 在全盘的总威胁分
    LL oppTotal = 0; // 对手在全盘的总威胁分

    // 步骤 1: 遍历棋盘所有行
    for (int i = 0; i < BOARD_SIZE; i++) {
        // 步骤 2: 遍历棋盘所有列
        for (int j = 0; j < BOARD_SIZE; j++) {
            const Coord p = {i, j, 0}; // 创建坐标

            // 步骤 3: 如果是 AI 的棋子
            if (board->layout[i][j] == gAiPlayerId) {
                // 步骤 4: 计算这个 AI 棋子产生的威胁分
                aiTotal += getPlayerThreat(board, p, gAiPlayerId);
                // 步骤 5: 如果是对手的棋子
            } else if (board->layout[i][j] == gOppPlayerId) {
                // 步骤 6: 计算这个对手棋子产生的威胁分
                oppTotal += getPlayerThreat(board, p, gOppPlayerId);
            }
        }
    }

    // 步骤 7: 最终静态分数 = AI总威胁 - 对手总威胁
    return aiTotal - oppTotal;
}

// --- 候选着法生成 --- //

/**
 * @brief 检查 (r, c) 附近 (2格内) 是否有棋子
 * (这是 "只在棋子附近落子" 启发规则的实现)
 * @param board (只读) 棋盘状态
 * @param r 行
 * @param c 列
 * @return 1 (附近有子) 或 0 (附近无子)
 */
int isNearPiece(const ChessBoard *board, const int r, const int c) {
    // 步骤 1: 遍历 8 个方向
    for (int d = 0; d < 8; d++) {
        // 步骤 2: 检查 1 到 2 格的距离
        for (int dist = 1; dist <= 2; dist++) {
            // 步骤 3: 定义 8 个方向的 (dx, dy)
            const int directions[8][2] = {
                {-1, 0}, {1, 0}, {0, -1}, {0, 1}, // 上下左右
                {-1, -1}, {-1, 1}, {1, -1}, {1, 1} // 4个斜向
            };
            // 步骤 4: 计算目标坐标
            const int newRow = r + directions[d][0] * dist;
            const int newCol = c + directions[d][1] * dist;

            // 步骤 5: 检查坐标是否在棋盘内
            if (newRow >= 0 && newRow < BOARD_SIZE && newCol >= 0 && newCol < BOARD_SIZE) {
                // 步骤 6: 检查该点是否有棋子 (非空)
                if (board->layout[newRow][newCol] != EMPTY_SLOT) {
                    return 1; // 附近有子, 返回 1 (true)
                }
            }
        }
    }
    // 步骤 7: 遍历完所有方向和距离, 附近无子
    return 0;
}

/**
 * @brief qsort 比较函数 (按分数降序排列)
 */
int compareCoordsQsort(const void *a, const void *b) {
    const Coord *posA = (Coord *) a;
    const Coord *posB = (Coord *) b;

    // 步骤 1: 如果 A < B, 返回 1 (表示 A 应该在 B 后面)
    if (posA->score < posB->score) return 1;
    // 步骤 2: 如果 A > B, 返回 -1 (表示 A 应该在 B 前面)
    if (posA->score > posB->score) return -1;
    // 步骤 3: 如果 A == B, 返回 0
    return 0;
}

/**
 * @brief 生成候选着法列表，并按启发式分数排序
 * @param board (只读) 棋盘状态
 * @param list (出参) 指向 CandidateList 的指针，用于填充
 */
void generateCandidates(const ChessBoard *board, CandidateList *list) {
    // 步骤 1: 初始化列表
    list->count = 0;
    LL hScore = 0; // 临时存储启发分
    int firstZero = 1; // 标记是否已添加了第一个 0 分着法 (作为备选)

    // 步骤 2: 遍历棋盘所有行
    for (int i = 0; i < BOARD_SIZE; i++) {
        // 步骤 3: 遍历棋盘所有列
        for (int j = 0; j < BOARD_SIZE; j++) {
            // 步骤 4: 检查是否为 "好" 的候选点
            // 规则: 1. 必须是空点 2. 必须在现有棋子 2 格范围内
            if (board->layout[i][j] == EMPTY_SLOT && isNearPiece(board, i, j)) {
                // 步骤 5: 计算该点的启发式分数 (进攻分 + 防守分)
                const Coord tempPos = {i, j, 0};
                hScore = getPositionHeuristic(board, tempPos);

                // 步骤 6: 只添加一个 0 分着法 (保证有棋可走)
                if (hScore == 0 && firstZero) {
                    list->candidates[list->count] = tempPos;
                    list->candidates[list->count].score = hScore;
                    list->count++;
                    firstZero = 0; // 不再添加 0 分着法

                    // 步骤 7: 添加所有 > 0 分的着法
                } else if (hScore > 0) {
                    list->candidates[list->count] = tempPos;
                    list->candidates[list->count].score = hScore;
                    list->count++;
                }
            }
        }
    }

    // 步骤 8: 着法排序 (Move Ordering)
    // 这是 Alpha-Beta 剪枝效率的关键!
    // 优先搜索分数高 (最可能) 的着法, 可以极大提高剪枝率
    if (list->count > 1) {
        qsort(list->candidates, list->count, sizeof(Coord), compareCoordsQsort);
    }

    // 步骤 9: 候选着法剪枝 (Beam Search)
    // 限制搜索宽度, 只考虑最好的 N 个着法
    // 这里限制为 6, 大幅减少搜索空间, 提高速度
    list->count = list->count > 6 ? 6 : list->count;
}

// --- Alpha-Beta 搜索 --- //

/**
 * @brief Alpha-Beta 剪枝搜索 (核心)
 * @param board (可写) 棋盘状态 (函数会进行落子和悔棋)
 * @param depth 剩余搜索深度
 * @param alpha Alpha 值 (我方能保证的最低分)
 * @param beta Beta 值 (对手能保证的最高分)
 * @param player 当前轮到谁 (AI 或 Opponent)
 * @param lastMove 上一步的落子 (用于胜负判断)
 * @return 当前局面的评估分数
 */
LL alphaBeta(ChessBoard *board, const int depth, LL alpha, LL beta, const int player, const Coord lastMove) {
    // --- 步骤 1: 置换表查找 ---
    // 在搜索开始时, 立即查询置换表
    const LL hashVal = ttSearch(board->currentHash, depth, alpha, beta);
    if (hashVal > SCORE_MIN - 1LL) {
        // 如果命中 (分数有效), 直接返回存储的分数, 剪掉整个子树
        return hashVal;
    }

    // --- 步骤 2: 胜负判断 (基于上一步) ---
    // (这是 "达到叶节点" 的一种情况: 游戏已结束)
    // 检查 *上一步* 的落子 (lastMove) 是否导致了胜利

    // 2a: 如果当前是 AI 走, 检查 对手 的上一步 (lastMove) 是否获胜
    if (player == gAiPlayerId && getPlayerThreat(board, lastMove, gOppPlayerId) >= 1111111111LL) {
        // 对手赢了, 返回一个极低分 (输棋)
        return SCORE_MIN + 1LL; // +1 是为了与 "未命中" 区分
    }
    // 2b: 如果当前是 对手 走, 检查 AI 的上一步 (lastMove) 是否获胜
    if (player == gOppPlayerId && getPlayerThreat(board, lastMove, gAiPlayerId) >= 1111111111LL) {
        // AI 赢了, 返回一个极高分 (赢棋)
        return SCORE_MAX - 1LL; // -1 是为了与 "未命中" 区分
    }

    // --- 步骤 3: 达到搜索深度 (叶节点) ---
    if (depth == 0) {
        // 3a: 搜索已达最大深度, 调用静态评估函数
        const LL boardScore = evaluateBoardScore(board);
        // 3b: 将评估结果存入置换表 (精确值)
        ttStore(board->currentHash, depth, boardScore, TT_TYPE_EXACT);
        // 3c: 返回静态评估分
        return boardScore;
    }

    // --- 步骤 4: 生成与排序候选着法 ---
    CandidateList list;
    generateCandidates(board, &list);

    // 4a: 默认的哈希存储类型为 ALPHA (下界)
    // (表示我们至少找到了一个分数为 alpha, 但可能被 Beta 剪枝)
    int hashType = TT_TYPE_ALPHA;

    // --- 步骤 5: 无棋可走 (平局或结束) ---
    // (这是 "达到叶节点" 的另一种情况: 棋盘已满)
    if (list.count == 0) {
        // 5a: 没有候选着法, 只能评估当前局面
        const LL boardScore = evaluateBoardScore(board);
        // 5b: 存入置换表
        ttStore(board->currentHash, depth, boardScore, TT_TYPE_EXACT);
        // 5c: 返回分数
        return boardScore;
    }

    // --- 步骤 6: 递归搜索 ---
    // 初始化为 负无穷(AI) 或 正无穷(对方)
    LL maxMinEval = player == gAiPlayerId ? SCORE_MIN : SCORE_MAX;

    // 遍历所有 (已排序的) 候选着法
    for (int i = 0; i < list.count; i++) {
        // 6-1: 落子 (更新棋盘和哈希)
        boardUpdate(board, list.candidates[i].row, list.candidates[i].col, player);
        // 6-2: 递归调用 (深度-1, 轮到对手, 传入刚下的子)
        const LL eval = alphaBeta(board, depth - 1, alpha, beta, 3 - player, list.candidates[i]);
        // 6-3: 恢复棋盘和哈希 (悔棋)
        boardUpdate(board, list.candidates[i].row, list.candidates[i].col, EMPTY_SLOT);
        // 6-4: 更新此节点的最高/最低分
        if ((eval > maxMinEval && player == gAiPlayerId) || (eval < maxMinEval && player == gOppPlayerId)) {
            maxMinEval = eval;
        }
        if (eval > alpha && player == gAiPlayerId) {
            // 6-5A: 更新 Alpha (我方能保证的最低分)
            alpha = eval;
            hashType = TT_TYPE_EXACT;
        } else if (eval < beta && player == gOppPlayerId) {
            // 6-5B: 更新 Beta (对手能保证的最高分)
            beta = eval;
            hashType = TT_TYPE_EXACT;
        }
        // 6-6: Beta 剪枝
        if (beta <= alpha) {
            // a.如果我方能保证的分 (alpha) 已经 >= 对手在父节点能保证的分 (beta)
            // a.那么对手 (Minimizer) 绝不会选择进入这个分支

            // b.如果对手能保证的分 (beta) 已经 <= 我方在父节点能保证的分 (alpha)
            // b.那么我方 (Maximizer) 绝不会选择进入这个分支
            hashType = player == gAiPlayerId ? TT_TYPE_BETA /* 标记为 Beta (上界), 因为分数冲破了 beta*/ : TT_TYPE_ALPHA /* 标记为 Alpha (下界), 因为分数跌破了 alpha */;
            break; // 停止搜索
        }
    }
    // 6-7: 存储结果
    ttStore(board->currentHash, depth, maxMinEval, hashType);
    // 6-8: 返回此节点找到的 最高(我方) 最低(对方) 分数
    return maxMinEval;
}

/**
 * @brief 寻找最佳着法 (搜索入口)
 * (这是 Alpha-Beta 的 "根节点" )
 * @param board (可写) 当前的棋盘状态
 * @return 最佳着法 (Coord)
 */
Coord determineNextPlay(ChessBoard *board) {
    // 步骤 1: 为本次决策清空置换表 (可选, 但通常是好的)
    memset(gTranspositionTable, 0, sizeof(TT_Entry) * TT_SIZE);

    // 步骤 2: 生成第一层 (根节点) 的候选着法
    CandidateList list;
    generateCandidates(board, &list);

    // 步骤 3: 初始化最佳分数和最佳着法
    LL bestScore = SCORE_MIN; // AI 是 Maximizer, 寻找最高分
    Coord bestMove = {-1, -1, 0}; // 默认无效着法

    // 步骤 4: 设置保底着法 (如果列表非空)
    if (list.count > 0) {
        bestMove = list.candidates[0]; // 至少返回排序后的第一个 (最好的)
    }

    // 步骤 5: 迭代第一层 (模拟 Alpha-Beta 的根节点)
    for (int i = 0; i < list.count; i++) {
        // 步骤 5a: 落子 (AI下)
        boardUpdate(board, list.candidates[i].row, list.candidates[i].col, gAiPlayerId);

        // 步骤 5b: 调用 Alpha-Beta (注意: 深度是 SEARCH_DEPTH (7), 轮到对手 gOppPlayerId)
        // 这将启动一个 7 层的搜索 (总共 1+7=8 层)
        const LL score = alphaBeta(board, SEARCH_DEPTH, SCORE_MIN, SCORE_MAX, gOppPlayerId, list.candidates[i]);

        // 步骤 5c: 悔棋
        boardUpdate(board, list.candidates[i].row, list.candidates[i].col, EMPTY_SLOT);

        // 步骤 5d: 比较并更新最佳着法
        if (score > bestScore) {
            bestScore = score; // 找到了一个更好的分数
            bestMove = list.candidates[i]; // 更新最佳着法
        }
    }

    // 步骤 6: 返回找到的最佳着法
    return bestMove;
}

// --- 主函数 --- //

/**
 * @brief 主函数
 * @return 0
 */
int main() {
    // --- 步骤 1: 全局初始化 ---
    loadPatternScores(); // 计算对手棋型分
    ttInit(); // 初始化 Zobrist 键和置换表

    // --- 步骤 2: 主循环 (读取命令并响应) ---
    char line_buffer[256]; // 定义一个足够大的行缓冲区
    char input[20]; // 命令缓冲区
    Coord movePos;

    // 使用 fgets 循环读取一整行
    while (fgets(line_buffer, sizeof(line_buffer), stdin) != NULL) {
        // 尝试从行缓冲区中解析出第一个指令
        if (sscanf(line_buffer, "%s", input) != 1) {
            continue; // 如果是空行或无效输入，则跳过
        }

        // 步骤 2c: 处理 "START" 命令
        if (strcmp(input, "START") == 0) {
            // 从 line_buffer 中解析 "START" 之后的数字
            if (sscanf(line_buffer, "START %d", &gAiPlayerId) == 1) {
                gOppPlayerId = gAiPlayerId == 1 ? 2 : 1; // 确定对手颜色
                boardInit(&gCurrentBoard); // 初始化棋盘 (中心4子)
                // 做出响应
                printf("OK\n");
                fflush(stdout);
            }

            // 步骤 2d: 处理 "PLACE" 命令 (对手落子)
        } else if (strcmp(input, "PLACE") == 0) {
            // 从同一行中解析 "PLACE" 之后的两个数字
            if (sscanf(line_buffer, "PLACE %d %d", &movePos.row, &movePos.col) == 2) {
                // 更新棋盘
                boardUpdate(&gCurrentBoard, movePos.row, movePos.col, gOppPlayerId);
            }

            // 步骤 2e: 处理 "TURN" 命令 (轮到 AI)
        } else if (strcmp(input, "TURN") == 0) {
            // 决定下一步
            const Coord nextMove = determineNextPlay(&gCurrentBoard);
            // 输出走法
            printf("%d %d\n", nextMove.row, nextMove.col);
            fflush(stdout);
            // 更新棋盘
            boardUpdate(&gCurrentBoard, nextMove.row, nextMove.col, gAiPlayerId);

            // 步骤 2f: 处理 "END" 命令
        } else if (strcmp(input, "END") == 0) {
            break; // 退出主循环
        }
    }

    // --- 步骤 3: 清理 ---
    free(gTranspositionTable); // 释放置换表的内存
    return 0;
}
