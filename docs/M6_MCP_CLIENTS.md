# M6：MCP 客户端接入（OpenCode / DeepSeek harness）

记录时间：2026-11-08
状态：调研完成。OpenCode 与 DeepSeek harness 的远程 / stdio 两种接法均已在本机实测；`pvforge-mcp` 尚未实现，第 6 节是接入契约。
关联：[M6_PLAN.md](M6_PLAN.md) 第 2 章。

## 结论

| 客户端 | 推荐接法 | 备注 |
|---|---|---|
| OpenCode 2.x | Streamable HTTP（`type: "remote"`）或 stdio 桥 | 远程配置里的 URL 必须写死；端口/token 每次启动会变时，用桥更稳 |
| DeepSeek harness（`dsh`） | Streamable HTTP（`transport: streamable-http`）或 stdio 桥 | 配置写在 profile 的 `cordis.patch.yml`，新增条目必须放在 `- insert:` 下 |
| Claude Desktop | **只能** stdio 桥 | 自定义 connector 从 Anthropic 云端发起，连不到 `127.0.0.1` |
| Claude Code / Cursor | HTTP 或 stdio 桥 | 见附录 |

PVForge 的端口和 token 默认每次启用都会变化，静态的远程客户端配置很容易过期。日常使用优先 `pvforge-mcp` stdio 桥：它每次启动读取 `~/.config/PVForge/mcp-endpoint.json`，自动跟随当前实例；只有在固定端口、一次性调试，或客户端配置由 PVForge 当场生成时，才直接用远程 URL。

## 1. 本次实测环境

| 组件 | 版本 / 路径 |
|---|---|
| OpenCode CLI | 2.1.4（`/home/xmb505/.opencode/bin/opencode`） |
| OpenCode 后台服务 | 2.2.6（桌面应用的 `opencode-cli serve --service`；`opencode mcp list` 实际走它） |
| OpenCode 全局配置 | `~/.config/opencode/opencode.jsonc`（只读查看，未修改） |
| DeepSeek harness | `dsh` 0.2.2-rc.1 |
| DeepSeek MCP 插件 | `@deepseek-ai/dsh-mcp-client` 0.2.1-rc.5 |
| dsh profile | `~/.dsh/profiles/web` |
| 实测平台 | Linux |

实测方法：在 `/tmp` 起一个最小 Streamable HTTP MCP 服务器，监听 `127.0.0.1:39322`，要求 `Authorization: Bearer test-token-123`，提供 `echo` 工具，响应既可为 JSON 也可为 SSE。分别用 OpenCode 和 `dsh` 连接：

- OpenCode 远程配置：`opencode mcp list` 显示 `✓ pvforge-test connected`；服务器收到 `initialize`（协议 `2025-11-25`）、`notifications/initialized`、`GET /mcp`、`tools/list`。
- OpenCode stdio 配置：`type: "local"` 启动临时 stdio→HTTP 桥，显示 connected；服务器收到 `initialize`、`notifications/initialized`、`tools/list`。
- `dsh` 远程配置：启动时先发 `server/discover`，服务器不支持后回退 `initialize`（`2025-11-25`），随后 `tools/list`。
- `dsh` stdio 配置：桥接进程成功连接，行为同上。

这说明下面这些格式不是纸面推测，而是当前版本实际接受的。OpenCode 的 MCP 连接由后台服务执行，所以下面 OpenCode 的实测结果对应 2.2.6 服务；CLI 2.1.4 只用于命令行语法。未实测的是 PVForge 服务端本身（M6 尚未实现）和 `pvforge-mcp` 二进制。

## 2. 传输矩阵

| 客户端 | 版本 | stdio | Streamable HTTP | 旧版 HTTP+SSE | 备注 |
|---|---|---|---|---|---|
| OpenCode V2 | 2.x（本机 CLI 2.1.4 / 后台服务 2.2.6） | 支持，`type: "local"` | 支持，`type: "remote"` + `url` + `headers` | **不支持** | V2 只走 Streamable HTTP，不再回退 SSE |
| OpenCode V1 | 1.18.x 及更早 | 支持 | 支持 | 支持，POST 失败后回退 GET SSE | 配置为扁平的 `mcp.<name>`，用 `enabled` |
| DeepSeek harness | 0.2.2-rc.1 | 支持，`transport: stdio` | 支持，`transport: streamable-http` | **不支持** | 优先 2026-07-28，失败后回退旧版；工具名 `mcp__<server>__<tool>` |
| Claude Desktop | 当前版 | 支持，`mcpServers` | 仅 Custom Connectors，走 Anthropic 云 | 不支持 | 云端连不到 localhost；本机 PVForge 只能用 stdio 桥 |
| Claude Code | 2.1.265+ | 支持 | 支持，`--transport http` | 支持，`--transport sse`；2.1.265+ 可自动回退 | JSON 里 `type: "http"`，也接受 `"streamable-http"` |
| Cursor | 当前版 | 支持 | 支持，`url` + `headers` | 支持 | 配置键是 `mcpServers` |

### 2.1 OpenCode 版本差异

**V1（1.18.x 及更早）**

- 配置形状是扁平的：

  ```jsonc
  {
    "mcp": {
      "pvforge": {
        "type": "remote",
        "url": "http://127.0.0.1:39321/mcp",
        "headers": { "Authorization": "Bearer <token>" },
        "enabled": true
      }
    }
  }
  ```

- 远程 `type: "remote"` 先尝试 Streamable HTTP POST；收到 4xx（例如 405）后回退到旧版 HTTP+SSE：`GET /sse`，读取 `endpoint` 事件，再 POST 到 `/message?sessionId=...`。
- `timeout` 是单个数字（毫秒）。

**V2（2.x；本机 CLI 2.1.4，MCP 连接由后台服务 2.2.6 执行）**

- `opencode` CLI 和后台服务可以是不同版本。`opencode mcp list` 的 MCP 连接由后台服务执行；本机后台服务是桌面应用自带的 2.2.6，所以下面以 2.2.6 的行为为准。
- 推荐形状是 `mcp.servers.<name>`：

  ```jsonc
  {
    "mcp": {
      "servers": {
        "pvforge": { "type": "remote", "url": "...", "headers": {} }
      }
    }
  }
  ```

- 用 `disabled: true` 停用；不要写 `enabled`。V2 仍接受旧的扁平形状做兼容（本机全局配置就是），但新配置统一用 `servers`。
- 远程服务器只实现 Streamable HTTP。SSE-only 的服务器在 V2 会直接失败，典型报错是 `Error POSTing to endpoint: (HTTP 405)`；这类服务器需要用 `mcp-remote` 之类的本地 stdio 包装，或等服务器迁移到 Streamable HTTP。
- `timeout` 是对象：`{ "startup": 30000, "catalog": 30000, "execution": 120000 }`。
- `codemode` 默认 `true`，MCP 工具会经 Code Mode 暴露；需要原生工具列表时设 `"codemode": false`。
- 工具名会规范化为 `<server>_<tool>`（非字母数字转 `_`）；Code Mode 下再按 server 分组。以实际 `tools/list` 和模型工具列表为准，不要硬编码。
- `protocol` 可选：`legacy`（默认，最高 2025-11-25）、`auto`、`2026-07-28`。PVForge 按 2025-11-25 实现即可，保持默认 `legacy`。
- `opencode mcp add` 在本机 CLI 2.1.4 上会写 `mcp.servers` 形状（已实测），例如：

  ```sh
  opencode mcp add pvforge --url http://127.0.0.1:39321/mcp \
    --header 'Authorization=Bearer <token>'
  ```

- 本机实测：在新目录里第一次、第二次运行 `opencode mcp list` 可能显示 `No MCP servers configured`，第三次才加载项目配置。这不是配置错误；重跑一次即可。用 `opencode debug config` 可以确认配置已经被解析。

### 2.2 DeepSeek harness 的传输与协议

- `dsh` 是启动器；profile 是插件补丁层栈。MCP 客户端插件是 `@deepseek-ai/dsh-mcp-client`。
- 支持的 `transport` 只有两个：`stdio`、`streamable-http`。没有 SSE。
- 官方 SDK 先尝试 2026-07-28：发 `server/discover`；服务器返回不支持后回退 `initialize`（实测回退到 2025-11-25）。PVForge 不必实现 2026-07-28，只要对未知方法返回 JSON-RPC 错误，客户端会回退。
- 工具在模型侧的名字以 `mcp__<serverName>__` 开头；原始名里的点等字符会按函数名规则规范化，有损时可能追加 hash 后缀。例如原始名 `project.summary` 可能显示为 `mcp__pvforge__project_summary`；以 `tools/list` 的结果为准，不要硬编码。
- 不支持 MCP prompt 模板、资源订阅；资源通过内置的资源读取工具按需读取。
- 启动连接失败默认不阻止 harness 启动，只是该服务器的工具不出现。调试时可设 `failOnStartupError: true`，让插件激活直接失败。

## 3. 服务端契约（PVForge 侧）

这一节是 M6 Stage 3 的实现验收口径。当前 `src/` 里还没有 MCP 服务端代码。

### 3.1 监听、认证与请求头

- 只绑定 `127.0.0.1`，端口自动分配（`0`）或用户指定。
- 每次启用生成新的随机 Bearer token；停止时立即失效。
- 只接受 loopback 来源；拒绝非本机、无 token、超大请求。
- 请求头：

| 头 | 方向 | 要求 | 值 |
|---|---|---|---|
| `Authorization` | 请求 | 必需 | `Bearer <token>`；scheme 大小写不敏感，token 精确匹配 |
| `Content-Type` | 请求 POST | 必需 | `application/json` |
| `Accept` | 请求 POST | 必需 | `application/json, text/event-stream`（两者都要声明） |
| `MCP-Protocol-Version` | 请求 | 初始化后建议带 | 协商后的版本，例如 `2025-11-25`；缺失时按 `2025-03-26` 兼容 |
| `Mcp-Session-Id` | 响应/请求 | 可选 | 只有实现有状态会话时才需要；v1 建议无状态，不发这个头 |
| `Content-Type` | 响应 | 必需 | `application/json` 或 `text/event-stream` |
| `WWW-Authenticate` | 401 响应 | 建议 | `Bearer` |

- 认证失败返回 `401`；不要返回 200 加 JSON-RPC 错误，客户端会把 401 当作认证问题。
- Host / Origin：必须校验 `Host`，只接受 `127.0.0.1:<port>`、`localhost:<port>`、`[::1]:<port>`；如果请求带 `Origin`，只接受本机来源，防止 DNS rebinding。本次测试服务器用 Node SDK 的 `localhostHostValidation` / `localhostOriginValidation` 放行了两者；PVForge 侧实现等价的校验即可。
- 不要接受 query string 里的 token；token 只从 `Authorization` 读。

### 3.2 端点文件

M6 计划只固定了路径和权限：

- Linux：`~/.config/PVForge/mcp-endpoint.json`（遵循 `$XDG_CONFIG_HOME`）
- 写入用 `QSaveFile` 或临时文件 + rename，权限 `0600`
- 停止 MCP 时删除
- 供 `pvforge-mcp` 读取，不要让客户端直接依赖这个文件

字段建议（M6 实现按此落地；如果改动，同步本文和 `pvforge-mcp`）：

```json
{
  "format": "pvforge-mcp-endpoint",
  "version": 1,
  "url": "http://127.0.0.1:39321/mcp",
  "token": "5tQ...43chars",
  "pid": 12345,
  "startedAt": "2026-11-08T09:00:00Z",
  "protocolVersion": "2025-11-25"
}
```

| 字段 | 类型 | 必需 | 说明 |
|---|---|---|---|
| `format` | string | 是 | 固定 `pvforge-mcp-endpoint`；桥拒绝其他值 |
| `version` | integer | 是 | 当前 `1` |
| `url` | string | 是 | 必须是 `http://127.0.0.1:<port>/mcp` 或 `http://[::1]:<port>/mcp` |
| `token` | string | 是 | 至少 32 字节随机数的 base64url；桥只放在 `Authorization` 头里 |
| `pid` | integer | 否 | PVForge 进程号，桥可用 `kill(pid, 0)` 判断是否过期 |
| `startedAt` | string | 否 | RFC 3339，便于排查 |
| `protocolVersion` | string | 否 | 提示值；以 initialize 实际协商结果为准 |

权限校验：POSIX 上桥应拒绝 group/other 可读的文件（mode 不是 `0600` 或 owner 不是当前用户）；Windows 上用当前用户 ACL。文件不存在或格式不对时，桥给出明确错误，不要静默连到默认端口。

### 3.3 HTTP 行为

- 每个 JSON-RPC 请求一个 `POST /mcp`。请求体是单个 JSON-RPC request / notification / response，不接受数组批量。
- 请求是 notification 或 response 时，服务器返回 `202 Accepted`，无 body。
- 请求是 request 时，服务器二选一：
  - `Content-Type: application/json`，返回一个 JSON 对象；
  - `Content-Type: text/event-stream`，用 SSE 返回，最终包含该请求的 JSON-RPC response，然后关闭流。
- 客户端必须同时支持这两种响应。**建议 v1 用 `application/json`**，实现简单；`preview.render_frame` 这类大结果也走 JSON。
- `GET /mcp` 用于服务器主动推送。v1 不需要服务器主动通知，返回 `405 Method Not Allowed` 即可；客户端必须容忍 405。实测 OpenCode 和 `dsh` 都会发 GET，M6 测试要覆盖 405 的情况。
- `DELETE /mcp` 用于结束会话；无状态实现返回 405 即可。
- 建议 v1 无状态：initialize 响应不发 `Mcp-Session-Id`，后续请求也不要求。实测无状态服务器可以同时被 OpenCode 和 `dsh` 正常连接。
- 协议版本：至少支持 `2025-06-18` 和 `2025-11-25`。对 `server/discover` 返回 `-32601 Method not found`，`dsh` 会自动回退 `initialize`（已实测）。
- v1 必须实现的方法：`initialize`、`notifications/initialized`、`ping`、`tools/list`、`tools/call`、`resources/list`、`resources/read`、`notifications/cancelled`。资源可以为空列表，但方法要存在。
- 限额：
  - 请求体上限建议 32 MiB（预览 PNG 的 base64 可能较大）；
  - `preview.render_frame` 限流 2 次/秒；
  - 单 blob ≤ 5 MiB；
  - 单批 ≤ 200 操作；
  - 超限返回 `413` 或 JSON-RPC `-32602`，并写日志。
- 错误码：
  - `401` 认证失败；
  - `403` Host / Origin 校验失败；
  - `404` 路径错误或会话过期；
  - `405` 方法不支持（GET/DELETE 可接受）；
  - `406` 客户端没有同时接受 JSON 和 SSE；
  - `413` 请求体过大；
  - JSON-RPC 业务错误用 `-32000` 到 `-32099`，`permission_denied` / `stopped_by_human` 放在 `error.data.code`。

## 4. OpenCode 配置

### 4.1 远程 Streamable HTTP

适合固定端口、一次性调试，或由 PVForge 生成配置后粘贴。V2 写法：

```jsonc
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "servers": {
      "pvforge": {
        "type": "remote",
        "url": "http://127.0.0.1:39321/mcp",
        "oauth": false,
        "headers": {
          "Authorization": "Bearer <token>"
        },
        "codemode": false,
        "timeout": {
          "startup": 30000,
          "catalog": 30000,
          "execution": 120000
        }
      }
    }
  }
}
```

要点：

- `oauth: false` 必须写。否则 OpenCode 默认把远程服务器当 OAuth 资源服务器，遇到 401 会尝试发现授权服务器。
- `codemode: false` 是可选项。设了之后工具直接出现在模型的原生工具列表里，便于审计；不设则走 Code Mode，工具按 server 分组。
- URL 和 header 里的 `{env:NAME}` 由 OpenCode 的后台 server 进程展开，不是由你当前 shell 里的 CLI 进程展开。桌面应用的后台服务通常先于 shell 启动，拿不到你刚 `export` 的变量；本机实测这种情况下 `"url": "{env:PVFORGE_MCP_URL}"` 会变成空字符串并报 `Invalid MCP server URL:`，header 也会缺失。要用环境变量，就让变量在后台服务启动前就存在（或重启服务），否则直接写 token，或用 stdio 桥。
- 全局配置放 `~/.config/opencode/opencode.jsonc`；项目配置放项目根的 `opencode.json` 或 `opencode.jsonc`。`opencode mcp add` 默认写项目配置，加 `--global` 写全局。

命令行等价写法：

```sh
opencode mcp add pvforge \
  --url http://127.0.0.1:39321/mcp \
  --header 'Authorization=Bearer <token>'
```

CLI 生成的条目不会自动加 `oauth: false`；用 Bearer token 时手动补上，避免 token 错误时触发 OAuth 发现。

### 4.2 stdio 桥（日常推荐）

配置里不放 token，桥自己读端点文件：

```jsonc
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "servers": {
      "pvforge": {
        "type": "local",
        "command": ["/usr/local/bin/pvforge-mcp"],
        "environment": {
          "PVFORGE_MCP_ENDPOINT": "/home/you/.config/PVForge/mcp-endpoint.json"
        },
        "timeout": {
          "startup": 30000,
          "catalog": 30000,
          "execution": 120000
        }
      }
    }
  }
}
```

- `command` 必须是绝对路径或确保在 OpenCode 的 `PATH` 里；桌面启动的 OpenCode 往往没有 shell 的 `PATH`。
- `environment` 会合并进子进程环境；`PVFORGE_MCP_ENDPOINT` 不匹配 dsh 的敏感名字清洗规则，可以放心用。
- 如果桥支持默认路径，这个环境变量可以省略；显式写出来便于排查。

### 4.3 V1 兼容写法

只在旧版 OpenCode 上使用：

```jsonc
{
  "mcp": {
    "pvforge": {
      "type": "remote",
      "url": "http://127.0.0.1:39321/mcp",
      "headers": { "Authorization": "Bearer <token>" },
      "enabled": true,
      "timeout": 30000
    }
  }
}
```

### 4.4 验证

```sh
opencode mcp list
```

预期：

```text
✓ pvforge  connected
```

如果显示 `failed: Error POSTing to endpoint: unauthorized (HTTP 401)`，是 token 不匹配；如果显示 `Invalid MCP server URL:`，检查 URL 里的 `{env:...}` 是否在后台服务进程里有值（桌面服务常拿不到 shell 变量），没有就写死；如果显示 `No MCP servers configured`，先确认 `opencode debug config` 能看到条目，再多跑一两次；刚配置完也可能短暂显示 `pending`，等几秒再查。

## 5. DeepSeek harness 配置

### 5.1 配置文件位置与补丁语法

- 当前 profile：`~/.dsh/profiles/web/cordis.patch.yml`
- 所有 profile 共用：`~/.dsh/cordis.patch.yml`（不存在就新建）
- `$DSH_HOME` 可覆盖默认的 `~/.dsh`

用户补丁层是 YAML 数组。**新增插件必须放在 `- insert:` 下面**：

```yaml
- insert:
    - id: mcp-pvforge
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        # ...
```

只写 `- id: ...` 而不加 `insert` 是“按 id 覆盖已有条目”。id 不存在时 `dsh` 会警告并跳过：

```text
dsh: [.../cordis.patch.yml] patch: entry "mcp-pvforge-bare" not found
```

这是本次实测确认的行为，和 README 里的最小配置片段容易混淆。

### 5.2 远程 Streamable HTTP

```yaml
- insert:
    - id: mcp-pvforge
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        serverName: pvforge
        transport: streamable-http
        url: http://127.0.0.1:39321/mcp
        headers:
          Authorization: !!js '`Bearer ${process.env.PVFORGE_MCP_TOKEN}`'
        toolCallTimeoutMs: 120000
```

- `!!js` 表达式在 harness 进程里求值，不经过 stdio 子进程的环境清洗，适合从环境变量取 token。
- 不想用表达式就直接写 `Authorization: Bearer <token>`；注意 YAML 里含冒号的值要加引号。
- `serverName` 必须匹配 `[A-Za-z0-9_-]{1,32}`，同一注册作用域内唯一。
- 工具名以 `mcp__pvforge__` 开头；原始名里的点等字符会按函数名规则规范化。

### 5.3 stdio 桥

```yaml
- insert:
    - id: mcp-pvforge
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        serverName: pvforge
        transport: stdio
        command: /usr/local/bin/pvforge-mcp
        args: []
        env:
          PVFORGE_MCP_ENDPOINT: /home/you/.config/PVForge/mcp-endpoint.json
        toolCallTimeoutMs: 120000
```

- stdio 子进程的环境会先做清洗：名字匹配 `/KEY|PASSWORD|SECRET|TOKEN/i` 的环境变量和所有 `DSH_*` 会被删掉，然后合并 `env` 里显式写的值。所以不要把 token 依赖在“继承环境”上；要么显式写进 `env`，要么让桥读端点文件（推荐）。
- 实测 `env` 里的值能正确传给 stdio 子进程。

### 5.4 字段速查

| 字段 | 默认 | 说明 |
|---|---|---|
| `transport` | 必填 | `stdio` 或 `streamable-http` |
| `serverName` | 必填 | 工具命名空间，`[A-Za-z0-9_-]{1,32}` |
| `command` / `args` / `env` / `cwd` | — | stdio 的可执行文件、参数、额外环境、工作目录 |
| `url` / `headers` | — | streamable-http 的端点与请求头 |
| `toolCallTimeoutMs` | `60000` | 单次 `tools/call` 或资源读取的超时 |
| `failOnStartupError` | `false` | 初始连接失败时是否让插件激活失败；调试时可开 |
| `reconnect.enabled` | `true` | 断线自动重连 |
| `reconnect.initialDelayMs` | `500` | 首次重连延迟，之后翻倍 |
| `reconnect.maxDelayMs` | `30000` | 退避上限，也是重置尝试预算的在线时长 |
| `reconnect.maxAttempts` | `10` | 连续失败上限 |

### 5.5 验证

```sh
# 确认条目被组合进配置
dsh --profile web --dump-config | grep -A12 'mcp-pvforge'

# 确认插件名和字段被 schema 接受（不会启动 harness）
dsh --profile web --dump-config-schema | grep -A5 'dsh-mcp-client'
```

启动后看日志里有没有 MCP 连接错误；工具出现后名字以 `mcp__pvforge__` 开头。首次调试可临时加 `failOnStartupError: true`，让连接失败直接暴露出来。

## 6. `pvforge-mcp` stdio 桥

当前状态：**未实现**。这一节是 M6 的实现契约和客户端配置依据。安装位置随 PVForge 安装包；下文用 `/usr/local/bin/pvforge-mcp` 举例。

### 6.1 职责

`pvforge-mcp` 是一个 stdio→Streamable HTTP 的桥：

- 从 stdin 读行分隔的 JSON-RPC，向 `POST <url>` 转发；
- 把 JSON 或 SSE 响应转回 stdout 的行分隔 JSON-RPC；
- 自动读取端点文件，跟随 PVForge 的端口 / token 变化；
- 处理 `Mcp-Session-Id`、`MCP-Protocol-Version`、`notifications/cancelled`；
- 只写 stdout 的 JSON-RPC，日志一律写 stderr。

### 6.2 启动参数与发现顺序

```text
pvforge-mcp [options]

  --endpoint <path>      端点 JSON 路径
  --url <url>            直接指定 URL（需同时给 --token）
  --token <token>        直接指定 token（不推荐，会出现在 ps）
  --wait <seconds>       启动时等待端点文件出现，默认 10
  --timeout <seconds>    单次 HTTP 请求超时，默认 120
  --log <path>           诊断日志路径，默认 stderr
  --selftest             读取端点，调用 initialize + tools/list，打印摘要后退出
  --version
  -h, --help
```

环境变量：

| 变量 | 作用 |
|---|---|
| `PVFORGE_MCP_ENDPOINT` | 端点文件路径 |
| `PVFORGE_MCP_URL` | 直接指定 URL |
| `PVFORGE_MCP_TOKEN` | 直接指定 token |
| `PVFORGE_MCP_TIMEOUT` | 单次请求超时（秒） |
| `PVFORGE_MCP_LOG` | 日志路径 |

优先级：命令行 > 环境变量 > 端点文件 > 平台默认路径。URL 和 token 必须成对出现；只给一个时报用法错误。

平台默认路径：

| 平台 | 路径 |
|---|---|
| Linux | `$XDG_CONFIG_HOME/PVForge/mcp-endpoint.json`，默认 `~/.config/PVForge/mcp-endpoint.json` |
| macOS | `~/Library/Application Support/PVForge/mcp-endpoint.json` |
| Windows | `%APPDATA%\PVForge\mcp-endpoint.json` |

M6 计划只写了 Linux 路径；macOS/Windows 路径是建议值，实现时确认。

### 6.3 行为约定

- 启动时最多等 `--wait` 秒让端点文件出现；超时则 stderr 输出“PVForge MCP 未启用或已停止”，退出码 `2`。客户端会显示服务器启动失败。
- 连接成功后，如果 PVForge 重启、端口变化、token 轮换：下一次请求时重新读端点文件并重新 `initialize`，不要直接退出。这样客户端不需要重启。
- `initialize` 响应里的 `Mcp-Session-Id` 保存下来，后续请求都带上；如果服务器返回 `404`，丢掉会话重新初始化一次。
- `MCP-Protocol-Version` 用 initialize 协商出的版本，初始化后的每个 POST 都带上；服务器不校验时也建议带。
- 每个 POST 都带：

  ```http
  Content-Type: application/json
  Accept: application/json, text/event-stream
  Authorization: Bearer <token>
  ```

- 响应处理：
  - `application/json`：原样把 JSON 写一行到 stdout；
  - `text/event-stream`：逐行解析 `data:`，把 JSON-RPC 消息写到 stdout；收到匹配当前请求 id 的 response 后可以结束该流；服务器发来的 notification 也要转发；
  - `202 Accepted`：notification/response 的正常结果，不写 stdout。
- 取消：stdin 收到 `notifications/cancelled` 时，立即 POST 该通知，并中止对应 id 的在途 fetch。
- 错误：HTTP 失败时向 stdout 写 JSON-RPC error（id 对应请求），`error.code = -32000`，`error.message` 带简短原因；不要把 HTTP body 里的 token 或路径原样回显。
- 退出码：`0` 正常；`2` 端点缺失/格式错误；`3` 认证失败；`4` 连接或协议失败；`5` 用法错误。
- 安全：只允许 `http://127.0.0.1:<port>/mcp` 和 `http://[::1]:<port>/mcp`；拒绝其他 host（除非未来加显式 `--allow-remote`）。不要日志记录 token；不要把 token 放进命令行参数（`ps` 可见）。
- 端点文件权限：POSIX 上拒绝非 `0600` 或 owner 不是当前用户的文件；Windows 上检查当前用户 ACL。

### 6.4 `--selftest` 输出

```text
endpoint: http://127.0.0.1:39321/mcp
server: PVForge 0.9.0
protocol: 2025-11-25
tools: 18
```

失败时打印原因并以非零退出。这个命令用于第 7 节的验证清单，不参与客户端连接。

## 7. 从启用 MCP 到客户端连上：分步验证

### 步骤 0：准备

- PVForge 已构建，`pvforge-mcp` 在 `PATH` 里（或知道绝对路径）。
- 客户端版本确认：`opencode --version`、`dsh --version`。
- Linux 上确认 `jq` 可用；没有就改用 Python 读 JSON。

### 步骤 1：在 PVForge 里启用

设置 → MCP：

- 勾选“本次启动启用”；
- 记下端口（或保持自动）；
- 复制 token；
- 权限先保持“只读”；
- 确认日志窗可用。

### 步骤 2：检查端点文件

```sh
EP="${XDG_CONFIG_HOME:-$HOME/.config}/PVForge/mcp-endpoint.json"
ls -l "$EP"
jq . "$EP"
```

预期：权限 `-rw-------`，JSON 里有 `url` 和 `token`，`url` 是 `127.0.0.1`。文件不存在说明 MCP 没启用或已经停止。

### 步骤 3：curl 验证 initialize

```sh
EP="${XDG_CONFIG_HOME:-$HOME/.config}/PVForge/mcp-endpoint.json"
URL="$(jq -r .url "$EP")"
TOKEN="$(jq -r .token "$EP")"

curl -sS --max-time 10 -D /tmp/pvf-headers.txt -o /tmp/pvf-body.txt \
  -X POST "$URL" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"curl","version":"0"}}}'

cat /tmp/pvf-headers.txt
cat /tmp/pvf-body.txt
```

预期：

- HTTP `200`；
- `Content-Type` 是 `application/json` 或 `text/event-stream`；
- body 里有 `serverInfo`、`capabilities.tools`、`protocolVersion`。

如果是 SSE，body 形如：

```text
event: message
data: {"jsonrpc":"2.0","id":1,"result":{...}}
```

### 步骤 4：curl 验证 initialized / tools/list

```sh
# notification，预期 202
curl -sS --max-time 10 -o /dev/null -w '%{http_code}\n' \
  -X POST "$URL" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'

# tools/list，预期 200 且包含 project.summary
curl -sS --max-time 10 \
  -X POST "$URL" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
```

### 步骤 5：桥自检

```sh
pvforge-mcp --selftest
```

预期退出码 `0`，打印 endpoint、server、protocol、tools 数量。失败时按第 8 节排查。

### 步骤 6：配置客户端

- OpenCode：用 4.2 的 stdio 配置（推荐）或 4.1 的远程配置。
- DeepSeek harness：用 5.3 的 stdio 配置或 5.2 的远程配置。
- 其他客户端：见附录。

### 步骤 7：客户端连接检查

OpenCode：

```sh
opencode mcp list
```

预期 `✓ pvforge connected`。第一次在新目录运行可能显示 `No MCP servers configured`，多跑一两次；刚启动也可能短暂显示 `pending`，等几秒再查。

DeepSeek harness：

```sh
dsh --profile web --dump-config | grep -A12 'mcp-pvforge'
```

启动后确认工具列表里有 `mcp__pvforge__` 前缀的工具。若启用了 `failOnStartupError: true`，连接失败会直接报错。

### 步骤 8：调用只读工具

让客户端调用：

- `project.summary`：返回尺寸、帧率、时长、活动摄像机、场景列表；
- `scene.list` / `scene.context`：返回当前场景和实体；
- `preview.render_frame`：返回指定帧 PNG。

PVForge 的 MCP 日志窗应出现对应行；状态为成功。若客户端看不到工具，先看 `tools/list` 是否返回，再看客户端是否过滤了工具。

### 步骤 9：验证写入边界

1. 保持只读，调用 `entity.update`，预期 `permission_denied`。
2. 在 PVForge 里点“允许修改”并确认。
3. 在当前场景内调用 `entity.create`，预期成功；日志窗出现 before/after。
4. 把播放头移到另一个场景，再调用写工具，预期 `permission_denied`，错误信息提示当前场景。
5. 调用 `entities.retire`，确认是“透明 0 + `_retired` + 锁定”，不是删除。

### 步骤 10：验证急停

1. 发起一个较长的调用（例如连续 `preview.render_frame` 或 `batch.run`）。
2. 点日志窗的“停止 AI”或按 `Ctrl+Shift+.`。
3. 预期当前调用返回 `stopped_by_human`，队列清空，批次快照恢复，写操作全部拒绝，读操作保留。
4. 重新“允许 AI 操作”，确认可以继续。

### 步骤 11：关闭与清理

关闭 MCP 后：

- 端点文件应被删除；
- `pvforge-mcp` 下一次请求应报端点缺失（退出码 2 或 JSON-RPC error）；
- 客户端显示 disconnected 或工具调用失败。

## 8. 常见错误与排查

| 现象 | 常见原因 | 处理 |
|---|---|---|
| `Connection refused` | PVForge 没启动、MCP 没启用、端口变了、端点文件过期 | 重新启用 MCP；`jq .url` 看当前 URL；确认没有连到旧端口 |
| `401 unauthorized` | token 不匹配、token 已重置、复制时带了换行/空格 | 重新复制 token；桥重读端点文件；检查 `Authorization` 值没有多余空白 |
| `403 Forbidden` | Host / Origin 校验失败 | 客户端 URL 用 `127.0.0.1`，不要用机器名或局域网 IP；不要手动加 `Origin` |
| `404 Not Found` | 路径不是 `/mcp`，或会话已过期 | 确认 URL 以 `/mcp` 结尾；有状态实现时重新 initialize |
| `405 Method Not Allowed` | 把 SSE-only 端点当 Streamable HTTP，或 GET 被拒 | PVForge 应支持 POST；GET 405 是允许的，客户端要容忍 |
| `406 Not Acceptable` | 请求没有同时接受 `application/json` 和 `text/event-stream` | 客户端/桥补上 `Accept`；自己用 curl 测试时也要带 |
| `413 Payload Too Large` | 预览图或 blob 超过请求体上限 | 降低预览分辨率/频率；检查 M6 限额 |
| OpenCode `Error POSTing to endpoint: (HTTP 405)` | V2 连了 SSE-only 服务器 | PVForge 用 Streamable HTTP；SSE-only 时改用 stdio 桥或 `mcp-remote` |
| OpenCode `Invalid MCP server URL:` | URL 用了 `{env:...}`，后台服务进程没有这个变量 | URL 写死；或把变量放进后台服务的启动环境/重启服务；或改用 stdio 桥 |
| OpenCode `No MCP servers configured` | 新目录首次运行、配置没被加载 | `opencode debug config` 确认；重跑；检查项目根文件名 |
| OpenCode `headers` 里的 `{env:...}` 不生效 | 替换发生在后台 server 进程；桌面服务没有你 shell 里的变量 | 同上；CLI 2.1.4 与后台 2.2.6 混用时尤其容易踩 |
| `dsh` 启动后没有工具 | 补丁没写 `- insert:`；serverName 重复；token 错；连接失败但默认不阻止启动 | `dsh --dump-config` 看条目；临时 `failOnStartupError: true`；看日志 |
| `dsh` 报 `patch: entry ... not found` | 新增条目写成了按 id 覆盖 | 外面包一层 `- insert:` |
| `dsh` stdio 子进程拿不到 token | 环境清洗删除了 `*TOKEN*` 变量 | 在 `env:` 里显式写，或让桥读端点文件 |
| Claude Desktop 里服务器不出现 | JSON 无效、只支持 stdio、没完全退出重启 | 用 JSON 校验器；确认 `mcpServers`；完全退出再开 |
| Claude Desktop 自定义 connector 连不上 | 连接从 Anthropic 云端发起，localhost 不可达 | 改用 `pvforge-mcp` stdio 配置 |
| Cursor `spawn ... ENOENT` | `command` 不在 PATH | 写绝对路径；确认 Node/Python 运行时存在 |
| 工具调用返回 `permission_denied` | 只读模式，或写到了当前场景之外 | 点“允许修改”；把播放头移到目标场景；检查根文件夹 |
| 急停后 AI 继续重试 | 客户端没区分 `stopped_by_human` | 客户端要把它当终止条件，不得自动重试；PVForge 写操作保持拒绝直到重新武装 |
| PVForge 重启后客户端全失败 | 端口/token 变了，静态远程配置过期 | 用 stdio 桥；或重新生成远程配置 |

## 9. 已知限制

- **Claude Desktop 不能直连 localhost。** 自定义 connector 由 Anthropic 云端拨号，`127.0.0.1` 不可达；本机 PVForge 只能用 stdio 桥。`claude_desktop_config.json` 也只启动 stdio 服务器。
- **OpenCode V2 不再支持 SSE-only。** V1 会在 POST 4xx 后回退旧版 HTTP+SSE；V2 只走 Streamable HTTP。PVForge 按 Streamable HTTP 实现，所以不受影响；未来如果加 SSE-only 端点，V2 需要桥。
- **OpenCode V2 的 Code Mode 默认开启。** 工具会按 server 分组，模型通过脚本调用；需要原生工具列表时设 `codemode: false`。
- **远程配置不能跟随动态端口。** `{env:...}` 在 OpenCode 的后台服务进程里展开，桌面服务通常没有你 shell 里的变量；PVForge 每次启动端口/token 变化时，静态远程配置会过期。日常用 stdio 桥。
- **token 明文风险。** 直接远程配置会把 Bearer token 写进客户端配置文件；stdio 桥把 token 留在 `0600` 的端点文件里，泄露面更小。无论哪种，都不要把 token 提交进仓库或贴进聊天记录。
- **loopback 没有 TLS。** 依赖本机回环 + Bearer token + 文件权限；同一用户的其他进程仍可能读到端点文件。M6 的 0600 和 owner 校验是最低要求。
- **MCP 默认关，重启不自动开。** 客户端在 PVForge 重启后会连接失败，直到人再次启用；工具会消失，这是预期行为。
- **多实例。** 端点文件指向最后一个启用的 PVForge 实例；同时开多个实例时，桥默认连最后一个。需要指定实例时用 `pvforge-mcp --url ... --token ...`。
- **IPv6。** 服务端只绑 `127.0.0.1` 时，客户端把 `localhost` 解析成 `::1` 会连接失败；配置里统一写 `127.0.0.1`。
- **大预览结果。** `preview.render_frame` 的 PNG base64 可能触发客户端的结果大小限制；M6 已限制 2 次/秒和边长 4096，客户端侧仍可能截断或拒绝。
- **dsh 不支持 MCP prompts / 资源订阅。** 资源只能按需读取；PVForge 的 `guide.get` 应同时做成工具，不要只做资源。
- **Claude Desktop 菜单和路径会随版本变化。** 附录里的路径以 2026-11 文档为准；升级后以应用内“Developer → Edit Config”为准。

## 10. 附录：其他客户端

### 10.1 Claude Desktop（只能 stdio）

配置文件：

- macOS：`~/Library/Application Support/Claude/claude_desktop_config.json`
- Windows：`%APPDATA%\Claude\claude_desktop_config.json`

```json
{
  "mcpServers": {
    "pvforge": {
      "command": "/usr/local/bin/pvforge-mcp",
      "args": [],
      "env": {
        "PVFORGE_MCP_ENDPOINT": "/home/you/.config/PVForge/mcp-endpoint.json"
      }
    }
  }
}
```

改完完全退出 Claude Desktop 再启动，否则不会重新拉起 stdio 进程。不要试图把 `url` / `type: "http"` 写进这个文件；Claude Desktop 会忽略或报错。

### 10.2 Claude Code

远程：

```sh
claude mcp add --transport http pvforge \
  http://127.0.0.1:39321/mcp \
  --header "Authorization: Bearer <token>"
```

stdio：

```sh
claude mcp add --transport stdio pvforge -- /usr/local/bin/pvforge-mcp
claude mcp list
```

Claude Code 的 JSON 配置里 `type` 用 `"http"`（也接受 `"streamable-http"`）；只有 `url` 没有 `type` 会被当成 stdio 并报配置错误。2.1.265+ 在 HTTP 失败后可以自动回退 SSE；更早版本用 `--transport sse`。

### 10.3 Cursor

项目配置 `.cursor/mcp.json`，全局配置 `~/.cursor/mcp.json`：

```json
{
  "mcpServers": {
    "pvforge": {
      "url": "http://127.0.0.1:39321/mcp",
      "headers": {
        "Authorization": "Bearer <token>"
      }
    }
  }
}
```

或 stdio：

```json
{
  "mcpServers": {
    "pvforge": {
      "command": "/usr/local/bin/pvforge-mcp",
      "args": [],
      "env": {
        "PVFORGE_MCP_ENDPOINT": "/home/you/.config/PVForge/mcp-endpoint.json"
      }
    }
  }
}
```

Cursor 支持 stdio、SSE、Streamable HTTP；远程条目没有显式 `type` 字段时按 `url` 推断。查看日志：Output 面板 → “MCP Logs”。

### 10.4 客户端配置对照

| 客户端 | 配置键 | 远程类型字段 | stdio 字段 | 环境变量替换 |
|---|---|---|---|---|
| OpenCode V2 | `mcp.servers` | `type: "remote"` | `type: "local"`, `command` 数组 | `{env:NAME}`（在后台 server 进程展开；桌面服务常拿不到 shell 变量） |
| OpenCode V1 | `mcp.<name>` | `type: "remote"` | `type: "local"` | 同版本 |
| DeepSeek harness | `cordis.patch.yml` 的 `insert` | `transport: streamable-http` | `transport: stdio` | `!!js` 表达式 |
| Claude Desktop | `mcpServers` | 不支持本机 HTTP | `command` / `args` / `env` | 无 |
| Claude Code | `mcpServers` / CLI | `type: "http"` | `command` / `args` / `env` | `${VAR}`（JSON 字符串） |
| Cursor | `mcpServers` | `url` + `headers` | `command` / `args` / `env` | `${env:NAME}` |

## 11. 参考

- OpenCode MCP 文档：https://opencode.ai/docs/mcp-servers/
- OpenCode V2 迁移（MCP 配置形状）：https://opencode.ai/v2/docs/migrate-v1/
- OpenCode 配置 schema：https://opencode.ai/config.json
- OpenCode V2 移除 SSE 回退的 issue：https://github.com/anomalyco/opencode/issues/61867
- OpenCode 增加 Streamable HTTP 的讨论：https://github.com/anomalyco/opencode/issues/8058
- MCP 传输规范（Streamable HTTP、会话、SSE、协议版本头）：https://modelcontextprotocol.io/specification/2025-06-18/basic/transports
- MCP 生命周期与版本协商：https://modelcontextprotocol.io/specification/2025-06-18/basic/lifecycle
- DeepSeek harness 仓库：https://github.com/deepseek-ai/deepseek-harness
- 本机 DeepSeek MCP 插件文档：`~/.dsh/profiles/node_modules/@deepseek-ai/dsh-mcp-client/README.zh.md`
- Claude Code MCP 文档：https://code.claude.com/docs/en/mcp
- Claude Desktop 自定义 connector（云端发起、不能连 localhost）：https://support.claude.com/en/articles/11175166-get-started-with-custom-connectors-using-remote-mcp
- Cursor MCP 文档：https://cursor.com/docs/context/mcp

## 12. 待验证项

| 项 | 现状 | 怎么验 |
|---|---|---|
| `pvforge-mcp` 实际二进制 | 本文是契约，未实现 | M6 Stage 3 实现后跑 `pvforge-mcp --selftest` 和第 7 节清单 |
| PVForge 服务端端点文件字段 | 计划只固定路径/权限，字段是建议 | 实现后对照 3.2；改字段同步本文 |
| 无状态 + GET 405 的客户端兼容 | 实测无状态服务器可连，但 GET 返回的是 SSE，不是 405 | M6 测试让 GET 返回 405，重跑 OpenCode/dsh |
| OpenCode `{env:...}` 与后台服务 | 本机 CLI 2.1.4、后台 2.2.6；桌面服务拿不到 shell 变量，实测 URL/header 为空 | 把变量放进服务启动环境或重启服务后复验；确认 2.2.6 文档行为 |
| dsh 的 2026-07-28 完整协商 | 实测 `server/discover` 后回退 2025-11-25 | 若 PVForge 实现 discover，再验 `protocol: auto` |
| macOS / Windows 端点路径与 ACL | 建议值 | 在对应平台安装后确认路径、权限、桥的 owner 校验 |
| Claude Desktop 菜单/路径 | 2026-11 文档 | 升级后以应用内 Developer → Edit Config 为准 |
| 大预览图在各客户端的限制 | 未测 | 用 4K PNG 调 `preview.render_frame`，看客户端是否截断 |
| 多 PVForge 实例 | 未测 | 同时开两个实例，确认端点文件指向和桥的 `--url` 覆盖 |
| IPv6 客户端行为 | 未测 | 服务端只绑 127.0.0.1，客户端用 `localhost` 复现 `::1` 失败 |
