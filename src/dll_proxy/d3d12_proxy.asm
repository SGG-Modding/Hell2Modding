option casemap:none

.data

PUBLIC oD3D12CreateDevice
PUBLIC oD3D12GetInterface
PUBLIC oD3D12SerializeRootSignature
PUBLIC oD3D12SerializeVersionedRootSignature
PUBLIC oD3D12CreateRootSignatureDeserializer
PUBLIC oD3D12CreateVersionedRootSignatureDeserializer
PUBLIC oD3D12GetDebugInterface
PUBLIC oD3D12DeviceRemovedExtendedData
PUBLIC oD3D12EnableExperimentalFeatures
PUBLIC oD3D12CoreCreateLayeredDevice
PUBLIC oD3D12CoreGetLayeredDeviceSize
PUBLIC oD3D12CoreRegisterLayers
PUBLIC oD3D12PIXEventsReplaceBlock
PUBLIC oD3D12PIXGetThreadInfo
PUBLIC oD3D12PIXNotifyWakeFromFenceSignal
PUBLIC oD3D12PIXReportCounter
PUBLIC oSetAppCompatStringPointer
PUBLIC oGetBehaviorValue

oD3D12CreateDevice                         qword 0
oD3D12GetInterface                         qword 0
oD3D12SerializeRootSignature               qword 0
oD3D12SerializeVersionedRootSignature      qword 0
oD3D12CreateRootSignatureDeserializer      qword 0
oD3D12CreateVersionedRootSignatureDeserializer qword 0
oD3D12GetDebugInterface                    qword 0
oD3D12DeviceRemovedExtendedData            qword 0
oD3D12EnableExperimentalFeatures           qword 0
oD3D12CoreCreateLayeredDevice              qword 0
oD3D12CoreGetLayeredDeviceSize             qword 0
oD3D12CoreRegisterLayers                   qword 0
oD3D12PIXEventsReplaceBlock                qword 0
oD3D12PIXGetThreadInfo                     qword 0
oD3D12PIXNotifyWakeFromFenceSignal         qword 0
oD3D12PIXReportCounter                     qword 0
oSetAppCompatStringPointer                 qword 0
oGetBehaviorValue                          qword 0

.code
fD3D12CreateDevice proc
    jmp qword ptr [oD3D12CreateDevice]
fD3D12CreateDevice endp

fD3D12GetInterface proc
    jmp qword ptr [oD3D12GetInterface]
fD3D12GetInterface endp

fD3D12SerializeRootSignature proc
    jmp qword ptr [oD3D12SerializeRootSignature]
fD3D12SerializeRootSignature endp

fD3D12SerializeVersionedRootSignature proc
    jmp qword ptr [oD3D12SerializeVersionedRootSignature]
fD3D12SerializeVersionedRootSignature endp

fD3D12CreateRootSignatureDeserializer proc
    jmp qword ptr [oD3D12CreateRootSignatureDeserializer]
fD3D12CreateRootSignatureDeserializer endp

fD3D12CreateVersionedRootSignatureDeserializer proc
    jmp qword ptr [oD3D12CreateVersionedRootSignatureDeserializer]
fD3D12CreateVersionedRootSignatureDeserializer endp

fD3D12GetDebugInterface proc
    jmp qword ptr [oD3D12GetDebugInterface]
fD3D12GetDebugInterface endp

fD3D12DeviceRemovedExtendedData proc
    jmp qword ptr [oD3D12DeviceRemovedExtendedData]
fD3D12DeviceRemovedExtendedData endp

fD3D12EnableExperimentalFeatures proc
    jmp qword ptr [oD3D12EnableExperimentalFeatures]
fD3D12EnableExperimentalFeatures endp

fD3D12CoreCreateLayeredDevice proc
    jmp qword ptr [oD3D12CoreCreateLayeredDevice]
fD3D12CoreCreateLayeredDevice endp

fD3D12CoreGetLayeredDeviceSize proc
    jmp qword ptr [oD3D12CoreGetLayeredDeviceSize]
fD3D12CoreGetLayeredDeviceSize endp

fD3D12CoreRegisterLayers proc
    jmp qword ptr [oD3D12CoreRegisterLayers]
fD3D12CoreRegisterLayers endp

fD3D12PIXEventsReplaceBlock proc
    jmp qword ptr [oD3D12PIXEventsReplaceBlock]
fD3D12PIXEventsReplaceBlock endp

fD3D12PIXGetThreadInfo proc
    jmp qword ptr [oD3D12PIXGetThreadInfo]
fD3D12PIXGetThreadInfo endp

fD3D12PIXNotifyWakeFromFenceSignal proc
    jmp qword ptr [oD3D12PIXNotifyWakeFromFenceSignal]
fD3D12PIXNotifyWakeFromFenceSignal endp

fD3D12PIXReportCounter proc
    jmp qword ptr [oD3D12PIXReportCounter]
fD3D12PIXReportCounter endp

fSetAppCompatStringPointer proc
    jmp qword ptr [oSetAppCompatStringPointer]
fSetAppCompatStringPointer endp

fGetBehaviorValue proc
    jmp qword ptr [oGetBehaviorValue]
fGetBehaviorValue endp

end