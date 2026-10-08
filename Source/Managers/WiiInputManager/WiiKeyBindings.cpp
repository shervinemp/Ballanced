#include "WiiKeyBindings.h"

#include <string.h>

namespace
{
    const char *kArrayName = "Keyboard";

    // DirectInput scan codes of the game's default bindings: the arrows,
    // Left Shift and Space.
    const CKDWORD kDefaultKeys[WiiKeyBindings::CONTROL_COUNT] = {0xC8, 0xD0, 0xCB, 0xCD, 0x2A, 0x39};
}

WiiKeyBindings::WiiKeyBindings() : m_ArrayId(0)
{
    memcpy(m_Keys, kDefaultKeys, sizeof(m_Keys));
}

CKDWORD WiiKeyBindings::GetDefaultKey(Control control)
{
    return kDefaultKeys[control];
}

void WiiKeyBindings::Update(CKContext *context)
{
    memcpy(m_Keys, kDefaultKeys, sizeof(m_Keys));

    CKObject *object = m_ArrayId ? context->GetObject(m_ArrayId) : NULL;
    if (!object || object->GetClassID() != CKCID_DATAARRAY || !object->GetName() ||
        strcmp(object->GetName(), kArrayName) != 0)
    {
        object = context->GetObjectByNameAndClass((CKSTRING)kArrayName, CKCID_DATAARRAY);
        m_ArrayId = object ? object->GetID() : 0;
    }
    CKDataArray *array = (CKDataArray *)object;
    if (!array || array->GetRowCount() < 1 || array->GetColumnCount() < CONTROL_COUNT)
        return;

    for (int c = 0; c < CONTROL_COUNT; ++c)
    {
        // Only a key parameter fits in a CKDWORD.
        if (array->GetColumnType(c) != CKARRAYTYPE_PARAMETER || array->GetColumnParameterGuid(c) != CKPGUID_KEY)
            continue;
        CKDWORD key = 0;
        if (array->GetElementValue(0, c, &key) && key > 0 && key < 256)
            m_Keys[c] = key;
    }
}

CKBOOL WiiKeyBindings::IsBound(CKDWORD key) const
{
    for (int c = 0; c < CONTROL_COUNT; ++c)
    {
        if (m_Keys[c] == key)
            return TRUE;
    }
    return FALSE;
}

void WiiKeyBindings::Apply(CKBYTE keys[256]) const
{
    CKBYTE held[CONTROL_COUNT];
    for (int c = 0; c < CONTROL_COUNT; ++c)
        held[c] = keys[kDefaultKeys[c]];
    // A default key bound to a control stands for that control only.
    for (int c = 0; c < CONTROL_COUNT; ++c)
    {
        if (IsBound(kDefaultKeys[c]))
            keys[kDefaultKeys[c]] = 0;
    }
    for (int c = 0; c < CONTROL_COUNT; ++c)
    {
        if (held[c])
            keys[m_Keys[c]] = 1;
    }
}
