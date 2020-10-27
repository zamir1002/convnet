#include "Model.h"

namespace dnn
{
	GlobalAvgPooling::GlobalAvgPooling(const dnn::Device& device, const dnnl::memory::format_tag format, const std::string& name, const std::vector<Layer*>& inputs) :
		Layer(device, format, name, LayerTypes::GlobalAvgPooling, 0, 0, inputs[0]->C, 1, 1, 1, 0, 0, 0, inputs),
		KernelH(inputs[0]->H),
		KernelW(inputs[0]->W),
		Scale(Float(1) / inputs[0]->H * inputs[0]->W),
		Kernel(dnnl::memory::dims({ dnnl::memory::dim(inputs[0]->H), dnnl::memory::dim(inputs[0]->W) })),
		Stride(dnnl::memory::dims({ dnnl::memory::dim(inputs[0]->H) , dnnl::memory::dim(inputs[0]->W) })),
		Padding(dnnl::memory::dims({ dnnl::memory::dim(0), dnnl::memory::dim(0) })),
		reorderFwdSrc(false),
		reorderBwdDiffSrc(false)
	{
		assert(Inputs.size() == 1);
	}

	std::string GlobalAvgPooling::GetDescription() const
	{
		std::string description = GetDescriptionHeader();

		description.append(nwl + " Scale:" + dtab + FloatToString(Scale));

		return description;
	}

	size_t GlobalAvgPooling::FanIn() const
	{
		return KernelH * KernelW;
	}

	size_t GlobalAvgPooling::FanOut() const
	{
		return 1;
	}

	void GlobalAvgPooling::InitializeDescriptors(const size_t batchSize)
	{
		std::vector<dnnl::memory::desc> memDesc = std::vector<dnnl::memory::desc>({ 
			dnnl::memory::desc(dnnl::memory::dims({ dnnl::memory::dim(batchSize), dnnl::memory::dim(InputLayer->C), dnnl::memory::dim(InputLayer->H), dnnl::memory::dim(InputLayer->W) }), dnnl::memory::data_type::f32, Format),
			dnnl::memory::desc(dnnl::memory::dims({ dnnl::memory::dim(batchSize), dnnl::memory::dim(C), 1, 1 }), dnnl::memory::data_type::f32, Format) });

		fwdDesc = std::make_unique<dnnl::pooling_forward::primitive_desc>(dnnl::pooling_forward::primitive_desc(dnnl::pooling_forward::desc(dnnl::prop_kind::forward_training, dnnl::algorithm::pooling_avg_exclude_padding, *InputLayer->DstMemDesc, memDesc[1], Stride, Kernel, Padding, Padding), Device.first));
		bwdDesc = std::make_unique<dnnl::pooling_backward::primitive_desc>(dnnl::pooling_backward::primitive_desc(dnnl::pooling_backward::desc(dnnl::algorithm::pooling_avg_exclude_padding, memDesc[0], fwdDesc->dst_desc(), Stride, Kernel, Padding, Padding), Device.first, *fwdDesc));
		
		fwd = std::make_unique<dnnl::pooling_forward>(dnnl::pooling_forward(*fwdDesc));
		bwd = std::make_unique<dnnl::pooling_backward>(dnnl::pooling_backward(*bwdDesc));

		reorderFwdSrc = fwdDesc->src_desc() != *InputLayer->DstMemDesc;
		reorderBwdDiffSrc = bwdDesc->diff_src_desc() != *InputLayer->DiffDstMemDesc;

		DstMemDesc = std::make_unique<dnnl::memory::desc>(fwdDesc->dst_desc());
		DiffDstMemDesc = std::make_unique<dnnl::memory::desc>(bwdDesc->diff_dst_desc());
	
		bwdAddDesc = std::make_unique<dnnl::binary::primitive_desc>(dnnl::binary::primitive_desc(dnnl::binary::desc(dnnl::algorithm::binary_add, *InputLayer->DiffDstMemDesc, *InputLayer->DiffDstMemDesc, *InputLayer->DiffDstMemDesc), Device.first));
		bwdAdd = std::make_unique<dnnl::binary>(dnnl::binary(*bwdAddDesc));
	}

	void GlobalAvgPooling::ForwardProp(const size_t batchSize, const bool training)
	{
		auto memSrc = dnnl::memory(*InputLayer->DstMemDesc, Device.first, InputLayer->Neurons.data());
		auto srcMem = reorderFwdSrc ? dnnl::memory(fwdDesc->src_desc(), Device.first) : memSrc;
		if (reorderFwdSrc)
		{
			dnnl::reorder(memSrc, srcMem).execute(Device.second, std::unordered_map<int, dnnl::memory>{ {DNNL_ARG_FROM, memSrc}, {DNNL_ARG_TO, srcMem} });
			Device.second.wait();
		}
			
		auto dstMem = dnnl::memory(*DstMemDesc, Device.first, Neurons.data());

		fwd->execute(Device.second, std::unordered_map<int, dnnl::memory>{ {DNNL_ARG_SRC, srcMem}, {DNNL_ARG_DST, dstMem} });
		Device.second.wait();

#ifndef DNN_LEAN
		if (training)
			ZeroFloatVector(NeuronsD1.data(), batchSize * PaddedCDHW);
#else
		DNN_UNREF_PAR(batchSize);
#endif
	}

	void GlobalAvgPooling::BackwardProp(const size_t batchSize)
	{
#ifdef DNN_LEAN
		ZeroGradient(batchSize);
#else
		DNN_UNREF_PAR(batchSize);
#endif // DNN_LEAN

		auto diffDstMem = dnnl::memory(*DiffDstMemDesc, Device.first, NeuronsD1.data());
		
		auto memDiffSrc = SharesInput ? dnnl::memory(*InputLayer->DiffDstMemDesc, Device.first) : dnnl::memory(*InputLayer->DiffDstMemDesc, Device.first, InputLayer->NeuronsD1.data());
		auto diffSrcMem = reorderBwdDiffSrc ? dnnl::memory(bwdDesc->diff_src_desc(), Device.first) : memDiffSrc;

		bwd->execute(Device.second, std::unordered_map<int, dnnl::memory> { {DNNL_ARG_DIFF_DST, diffDstMem}, {DNNL_ARG_DIFF_SRC, diffSrcMem} });
		Device.second.wait();

		if (reorderBwdDiffSrc)
		{
			dnnl::reorder(diffSrcMem, memDiffSrc).execute(Device.second, std::unordered_map<int, dnnl::memory>{ {DNNL_ARG_FROM, diffSrcMem}, {DNNL_ARG_TO, memDiffSrc} });
			Device.second.wait();
		}

		if (SharesInput)
		{
			bwdAdd->execute(Device.second, std::unordered_map<int, dnnl::memory>{ { DNNL_ARG_SRC_0, dnnl::memory(*InputLayer->DiffDstMemDesc, Device.first, InputLayer->NeuronsD1.data()) }, { DNNL_ARG_SRC_1, memDiffSrc }, { DNNL_ARG_DST, dnnl::memory(*InputLayer->DiffDstMemDesc, Device.first, InputLayer->NeuronsD1.data()) } });
			Device.second.wait();
		}
#ifdef DNN_LEAN
		ReleaseGradient();
#endif // DNN_LEAN
	}
}