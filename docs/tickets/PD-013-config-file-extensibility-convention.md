# PD-013 — 設定檔可擴充性慣例(跨 ticket 通用規則)

Phase 1 · docs · Depends on: —

- Source: `AGENTS.md`、`docs/design-spec.md` §10、`docs/tickets/PD-006-session-document-persistence.md`
- Origin: 2026-08-20 使用者要求「任何 config/setting 檔案都必須考量未來可擴充性」。這條要求跨越所有現在與未來的設定檔 ticket,不專屬於某一個,因此獨立成票,而不是塞進 PD-006——PD-006 的 scope 是「把 session document 寫對」,不是「訂出專案通用的設定檔慣例」,兩者混在一起會讓 PD-006 的 acceptance criteria 分不清哪些是它自己的責任、哪些是抄錄自通用規則。
- Priority: **中**——現在不緊急(下一個真正的設定檔實作是 PD-006,而 PD-006 本來就已經計畫做 schema version),但這條規則要在 PD-006 開工前落地存在,否則 PD-006 沒有東西可引用。

## Goal

把「設定檔可擴充性」寫成 `AGENTS.md` 的一條通用工程規則,讓它跟其他跨 ticket 硬規則(例如「Never persist a PIDL or a COM pointer」)一樣,對現在與未來的每一個設定檔 ticket 都生效,而不必每個 ticket 重新發明一次。

本 ticket **不**實作任何設定檔本身。它是規則落地,不是功能開發。

## 已確認的產品決策

1. 「可擴充性」的具體定義,對齊 PD-006 已經做的決策(PD-006 決策 #1、scope #1/#6),不重新發明:
   - 每個設定檔從第一個版本就帶明確的 schema version 欄位。
   - 讀取端對「未知欄位」采取保留而非丟棄的態度:反序列化時若遇到本版本不認識的欄位,寫回時不得靜默刪除它——這是本規則新增於 PD-006 現有決策之上的部分,PD-006 目前的 schema 是封閉、固定欄位,尚未处理這一點,留給 PD-006 交接區記錄是否需要補。
   - 版本遷移只做加法(新增欄位、新增可選狀態),不做破壞性重新解釋既有欄位語意;需要破壞性變更時,一律遞增 schema version 並寫遷移函式,不得原地改變舊版本欄位的意義。
   - 這條規則適用於「持久化到磁碟、跨啟動存活」的設定/狀態檔案,不適用於單次執行內的暫存資料結構。
2. **PD-006 已經滿足這條規則的核心部分**(schema version、遷移框架必須在位),本 ticket 不重寫 PD-006 的 scope,只確認並在 PD-006 交接區留一筆待做筆記(見下)。
3. `docs/tickets/PD-010-prototype-location-persistence.md` 的原型持久化檔案**明確排除**在這條規則之外——PD-010 自己的 ticket 文件已經寫明「明確不需要符合 §10 的 session document 契約」「原型檔案可拋棄」,這是既有、成立的決定,本 ticket 不否決它,只是重申規則的適用邊界不包含刻意設計成拋棄式的原型檔案。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

`docs/tickets/PD-006-session-document-persistence.md` 已確認的產品決策 #1:
> 從第一個版本就帶 schema version 並支援遷移。理由:事後補遷移機制需要處理「沒有版本號的檔案」,那比一開始就有更麻煩。

`docs/tickets/PD-010-prototype-location-persistence.md` Goal:
> 用最簡單的檔案格式。**明確不需要**符合 §10 的 session document 契約——schema version、原子寫入、備份保留與遷移都由 PD-006 定義,在這裡預先實作只會產生一份之後要丟掉的實作。

## Files to read and trace first

- `AGENTS.md`(整份,找到「Engineering rules」段落合適插入位置——建議放在「Never persist a PIDL or a COM pointer」附近,兩條都是關於持久化資料格式的硬規則)
- `docs/tickets/PD-006-session-document-persistence.md`(整份,尤其是已確認的產品決策與 scope)
- `docs/tickets/PD-010-prototype-location-persistence.md`(整份,確認排除範圍的措辭一致)
- `docs/design-spec.md` §10

## Scope

1. 在 `AGENTS.md` 的 Engineering rules 段落新增一條規則,內容涵蓋:schema version 必須存在、未知欄位讀取後寫回不得靜默丟棄、遷移只做加法。措辭要能被未來任何設定檔 ticket 直接引用為 binding constraint。
2. 在 PD-006 的 `## 交接區`(目前是空的 `<!-- 實作 agent 填寫,append-only -->` 佔位)**之前**的 Handoff requirements 段落,新增一行,要求 PD-006 的實作 agent 明確記錄:目前封閉 schema 的欄位集合是否需要為「未知欄位保留」預留設計(例如反序列化時把不認識的 JSON key 存進一個 side-map,寫回時原樣帶出),若判斷不需要也要寫出理由。**不是**新增 acceptance criteria(PD-006 尚未開工但範圍已經定案,本 ticket 不擴大它的驗收範圍),只是新增一項交接時必須回答的問題。
3. 不修改 PD-010 文件——它已經正確排除,不需要改動。

## Non-goals

- 不實作 session document、不動 PD-006 的 scope 或 acceptance criteria。
- 不引入任何 JSON 程式庫或版本協商程式碼。
- 不追溯要求 PD-010 的原型檔案支援可擴充性——它是刻意的拋棄式設計,見上。
- 不對「設定」(settings,例如側邊欄寬度)這個尚未定義的概念預先設計格式。

## Acceptance

1. `AGENTS.md` 的 Engineering rules 段落新增一條可擴充性規則,措辭包含 schema version、未知欄位保留、加法式遷移三個要素。
2. `docs/tickets/PD-006-session-document-persistence.md` 的 Handoff requirements 新增一行,要求交接時回答「未知欄位是否需要保留設計」。除此之外 PD-006 文件不變。
3. `docs/tickets/PD-010-prototype-location-persistence.md` 不變。
4. `docs/tickets.md` 的 Ticket 總覽新增本 ticket 一列,狀態為 `done`(規則落地即完成,沒有後續實作動作)。

## Agent checks

```powershell
rg -n "schema version|未知欄位|forward.compat" AGENTS.md
# 預期:命中新增的規則
git diff --check
```

## Handoff requirements

- 新規則的最終措辭(供之後任何設定檔 ticket 引用時複製)。
- 若判斷 PD-006 不需要為「未知欄位保留」預留設計,寫出理由(例如:schema 封閉且没有第三方或跨版本共用者,遷移函式本身就是保留機制,不需要額外的 side-map)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
