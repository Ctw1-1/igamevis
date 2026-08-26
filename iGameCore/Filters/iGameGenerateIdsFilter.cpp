#include "iGameGenerateIdsFilter.h"
#include <iGameAttributeSet.h>
#include <iGameDrawObject.h>
#include <iGamePointSet.h>
#include <iostream>

IGAME_NAMESPACE_BEGIN

iGameGenerateIdsFilter::iGameGenerateIdsFilter(IGenum dataType) {
    m_DataType = dataType;
    m_ArrayName = "Ids";
    m_StartId = 0;
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

bool iGameGenerateIdsFilter::Execute() {
    auto input = GetInput(0);
    if (input == nullptr) { return false; }

    if (m_DataType != IG_POINT && m_DataType != IG_CELL) { return false; }

    SetOutput(input);
    return Run();
}

bool iGameGenerateIdsFilter::Run() {
    auto output = GetOutput();
    IGsize count = 0;
    if (m_DataType == IG_POINT) {
        auto points = output->GetPoints();
        if (points == nullptr) { return false; }
        count = points->GetNumberOfPoints();
    } else {
        auto cells = output->GetCellArray();
        if (cells == nullptr) { return false; }
        count = cells->GetNumberOfCells();
    }

    if (count == 0) { return true; }

    DoubleArray::Pointer arr = DoubleArray::New();
    arr->SetName(m_ArrayName);
    arr->SetDimension(1);
    arr->Resize(count);

    for (IGsize i = 0; i < count; ++i) {
        arr->SetValue(i, static_cast<double>(m_StartId + static_cast<long long>(i)));
    }

    auto attrs = output->GetAttributeSet();
    if (attrs == nullptr) {
        output->SetAttributeSet(AttributeSet::New());
        attrs = output->GetAttributeSet();
    }

    const int existing = attrs->GetAttributeIndex(m_ArrayName);
    if (existing >= 0) {
        auto& attr = attrs->GetAttribute(existing);
        if (attr.attachmentType == m_DataType && attr.pointer) {
            attr.pointer = arr;
            attr.UpdateAllDataRange();
        } else {
            attrs->AddAttribute(IG_SCALAR, m_DataType, arr);
        }
    } else {
        attrs->AddAttribute(IG_SCALAR, m_DataType, arr);
    }

    std::cout << "[INFO] Added array '" << m_ArrayName << "' with " << count << " elements to "
              << (m_DataType == IG_POINT ? "Point" : "Cell") << " data." << std::endl;
    std::cout << "[INFO] Total attributes now: " << attrs->GetNumberOfAttributes() << std::endl;

    if (auto draw = DynamicCast<DrawObject>(output)) { draw->ForceReConvertToDrawableData(); }
    return true;
}

IGAME_NAMESPACE_END
