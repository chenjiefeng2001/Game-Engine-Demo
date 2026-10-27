namespace AvaloniaEditor.ViewModels;

/// <summary>
/// A3/D3：create 操作的**唯一**结果语义。
/// 生产 handler 与 gate 断言的是同一份 receipt，不再各说各话。
/// </summary>
public enum CreateEntityOutcome
{
    /// <summary>没有可用的 project/runtime，编辑被显式拒绝（未触达 host）。</summary>
    Refused,

    /// <summary>
    /// host 侧失败（返回无效 index 或抛出 typed failure）。
    /// ⚠ A3 closure 注记：本 outcome 的映射保留在代码里，但**没有生产证据**——
    /// managed-only 约束下两个 backend 都没有 create 失败注入点
    /// （`capi.cpp` 中 `LegacyFailureInjected()` 唯一作用于 `EditorSession_OpenProject`），
    /// A3 的 N3 负例因此裁决为 Refused（见 A3 contract §9 方案 A）。
    /// 若将来 native 新增 create 注入点，必须回头补这条分支的证据。
    /// </summary>
    Failed,

    /// <summary>创建成功；<see cref="CreateEntityReceipt.Index"/> 有效。</summary>
    Created,
}

/// <summary>
/// A3/D3：create 的结构化回执。
/// 成功必须带 index/name，失败必须带 diagnostic —— 空 diagnostic 不允许（禁止静默）。
/// </summary>
public readonly record struct CreateEntityReceipt(
    CreateEntityOutcome Outcome,
    int Index,
    string Name,
    string Diagnostic)
{
    public bool Succeeded => Outcome == CreateEntityOutcome.Created;

    public static CreateEntityReceipt Refused(string reason)
        => new(CreateEntityOutcome.Refused, -1, string.Empty, reason);

    public static CreateEntityReceipt Failed(string diagnostic)
        => new(CreateEntityOutcome.Failed, -1, string.Empty,
               diagnostic.Length > 0 ? diagnostic : "(host reported failure without a diagnostic)");

    public static CreateEntityReceipt Created(int index, string name)
        => new(CreateEntityOutcome.Created, index, name, string.Empty);

    public override string ToString()
        => Outcome switch
        {
            CreateEntityOutcome.Created => $"Created(idx={Index}, name='{Name}')",
            CreateEntityOutcome.Refused => $"Refused({Diagnostic})",
            _ => $"Failed({Diagnostic})",
        };
}
