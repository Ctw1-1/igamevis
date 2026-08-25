// ============================================================================
// ConvertToVertexFilter  — 将任意点集/网格转换为顶点单元（IG_VERTEX）网格
// 骨架由 scripts/igame_new_filter.py 生成，算法部分手写。
// ============================================================================
#include "iGameConvertToVertexFilter.h"

IGAME_NAMESPACE_BEGIN

ConvertToVertexFilter::ConvertToVertexFilter() {
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

bool ConvertToVertexFilter::Execute() {
    auto in = DynamicCast<PointSet>(GetInput(0));
    if (in.IsNull()) return false;

    auto out = UnstructuredMesh::New();
    out->SetName(in->GetName());

    switch (m_ConvertMethod) {
        case IG_CONVERT_POINT_TO_VERTEX:
            if (!ExecutePointToVertex(in, out)) return false;
            break;
        default:
            return false;
    }

    out->Modified();
    SetOutput(0, out);
    return true;
}

bool ConvertToVertexFilter::ExecutePointToVertex(PointSet::Pointer in, UnstructuredMesh::Pointer out) {
    auto inPoints = in->GetPoints();
    auto inAttrs = in->GetAttributeSet();
    auto outPoints = out->GetPoints();
    auto outAttrs = out->GetAttributeSet();

    IGsize pointNum = in->GetNumberOfPoints();
    outPoints->Reset();
    for (IGsize i = 0; i < pointNum; ++i) {
        // 顶点单元不存坐标，只存点下标；点坐标统一放在输出 Points 数组，
        // 对应关系由下标 i 保证。
        outPoints->AddPoint(in->GetPoint(i));
        igIndex cell[1] = {static_cast<igIndex>(i)};
        out->AddCell(cell, 1, IG_VERTEX);
        if ((i & 0x3FF) == 0) { UpdateProgress(static_cast<double>(i) / pointNum); }
    }

    // 属性处理：点属性一一对应，原样拷贝为输出点属性（IG_POINT）；
    // 顶点单元不复制点属性；原网格单元属性（IG_CELL）与输出顶点单元数量无法对应，丢弃。
    for (int i = 0; i < static_cast<int>(inAttrs->GetNumberOfAttributes()); ++i) {
        auto& attr = inAttrs->GetAttribute(i);
        if (attr.attachmentType != IG_POINT) continue;

        const IGsize valueCount = attr.pointer->GetNumberOfValues();

        FloatArray::Pointer pointArr = FloatArray::New();
        pointArr->SetName(attr.pointer->GetName());
        pointArr->SetDimension(attr.pointer->GetDimension());
        pointArr->Reserve(attr.pointer->GetNumberOfElements());
        for (IGsize v = 0; v < valueCount; ++v) { pointArr->AddValue(attr.pointer->GetValue(v)); }
        outAttrs->AddAttribute(attr.type, IG_POINT, pointArr);
    }


    UpdateProgress(1.0);
    return true;
}

IGAME_NAMESPACE_END
