// © 2021 NVIDIA Corporation

Result QueryPoolD3D12::Create(const QueryPoolDesc& queryPoolDesc) {
    D3D12_QUERY_HEAP_DESC desc = {};
    if (queryPoolDesc.queryType == QueryType::TIMESTAMP) {
        m_QuerySize = sizeof(uint64_t);
        m_QueryType = D3D12_QUERY_TYPE_TIMESTAMP;
        desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    } else if (queryPoolDesc.queryType == QueryType::TIMESTAMP_COPY_QUEUE) {
        // Prerequisite: D3D12_FEATURE_D3D12_OPTIONS3
        m_QuerySize = sizeof(uint64_t);
        m_QueryType = D3D12_QUERY_TYPE_TIMESTAMP;
        desc.Type = D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP;
    } else if (queryPoolDesc.queryType == QueryType::OCCLUSION) {
        m_QuerySize = sizeof(uint64_t);
        m_QueryType = D3D12_QUERY_TYPE_OCCLUSION;
        desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    } else if (queryPoolDesc.queryType == QueryType::PIPELINE_STATISTICS) {
#ifdef NRI_ENABLE_AGILITY_SDK_SUPPORT
        if (m_Device.GetDesc().features.meshShaderPipelineStats) {
            m_QuerySize = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1);
            m_QueryType = D3D12_QUERY_TYPE_PIPELINE_STATISTICS1;
            desc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1;
        } else {
#endif
            m_QuerySize = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
            m_QueryType = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
            desc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
#ifdef NRI_ENABLE_AGILITY_SDK_SUPPORT
        }
#endif
    } else if (queryPoolDesc.queryType == QueryType::ACCELERATION_STRUCTURE_SIZE) {
        m_QuerySize = sizeof(uint64_t);
        m_QueryType = QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE;
    } else if (queryPoolDesc.queryType == QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE || queryPoolDesc.queryType == QueryType::MICROMAP_COMPACTED_SIZE) {
        m_QuerySize = sizeof(uint64_t);
        m_QueryType = QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE;
    } else
        return Result::INVALID_ARGUMENT;

    if (m_QueryType >= QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE)
        return CreateBufferForAccelerationStructuresSizes(queryPoolDesc);

    desc.Count = queryPoolDesc.capacity;
    desc.NodeMask = NODE_MASK;

    HRESULT hr = m_Device->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_QueryHeap));
    RETURN_ON_BAD_HRESULT(&m_Device, hr, "ID3D12Device::CreateQueryHeap()");

    return Result::SUCCESS;
}

Result QueryPoolD3D12::CreateBufferForAccelerationStructuresSizes(const QueryPoolDesc& queryPoolDesc) {
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = (uint64_t)queryPoolDesc.capacity * m_QuerySize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

#ifdef NRI_ENABLE_AGILITY_SDK_SUPPORT
    if (m_Device.GetTightAlignmentTier() != 0)
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT;
#endif

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = m_Device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_BufferForAccelerationStructuresSizes));
    RETURN_ON_BAD_HRESULT(&m_Device, hr, "ID3D12Device::CreateCommittedResource()");

    return Result::SUCCESS;
}

ID3D12Resource* QueryPoolD3D12::GetBufferForAccelerationStructuresSizes(ID3D12GraphicsCommandList* commandList, bool isUAV) {
    ExclusiveScope lock(m_Lock);
    ID3D12Resource* buffer = m_BufferForAccelerationStructuresSizes.GetInterface();

    if (m_IsFirstTime)
        m_IsFirstTime = false; // promotion
    else if (m_IsUAV != isUAV) {
        // TODO: "bufferForAccelerationStructuresSizes" is completely hidden from a user, transition needs to be done under the hood (legacy barrier is used for simplicity)
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer;
        barrier.Transition.StateBefore = isUAV ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = isUAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_SOURCE;

        commandList->ResourceBarrier(1, &barrier);
    }

    m_IsUAV = isUAV;

    return buffer;
}
