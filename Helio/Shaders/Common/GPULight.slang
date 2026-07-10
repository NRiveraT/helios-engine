import Bindless;

struct GPULight
{
	float3 Position;
	uint Type;
	float3 Color;
	float Intensity;
	float3 Direction;
	float Range;
	float CosInner;
	float CosOuter;
	float2 _Pad2; 
};
static constexpr uint kLightStride = 64;

GPULight LoadGPULight(uint Slot, uint Index)
{
	ByteAddressBuffer B = GetStorageBuffer(Slot);
	uint Base = Index * kLightStride;
	
	GPULight L;
	L.Position = asfloat(B.Load3(Base + 0));
	L.Type = (B.Load(Base + 12));
	L.Color = asfloat(B.Load3(Base + 16));
	L.Intensity = asfloat(B.Load(Base + 28));
	L.Direction = asfloat(B.Load3(Base + 32));
	L.Range = asfloat(B.Load(Base + 44));
	L.CosInner = asfloat(B.Load(Base + 48));
	L.CosOuter = asfloat(B.Load(Base + 52));
	return L;	 
}