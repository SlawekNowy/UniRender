/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UNIRENDER_SHADER_HPP__
#define __UNIRENDER_SHADER_HPP__

#include "definitions.hpp"
#include "data_value.hpp"
#include "exception.hpp"
#include "scene_object.hpp"
#include <memory>
#include <functional>
#include "shader_nodes.hpp"
#include <sharedutils/datastream.h>
#include <sharedutils/util_hair.hpp>
#include <sharedutils/util_virtual_shared_from_this.hpp>

namespace unirender
{
	class NodeDesc;
	class GroupNodeDesc;
	struct NodeSocketDesc;
	struct DLLRTUTIL NodeDescLink
	{
		Socket fromSocket;
		Socket toSocket;
		void Serialize(DataStream &dsOut,const std::unordered_map<const NodeDesc*,uint64_t> &nodeIndexTable) const;
		void Deserialize(GroupNodeDesc &groupNode,DataStream &dsIn,const std::vector<const NodeDesc*> &nodeIndexTable);
	};

	enum class SocketIO : uint8_t
	{
		None = 0u,
		In = 1u,
		Out = In<<1u
	};

	struct NodeSocketDesc
	{
		SocketIO io = SocketIO::None;
		DataValue dataValue {};
		void Serialize(DataStream &dsOut) const;
		static NodeSocketDesc Deserialize(DataStream &dsIn);
	};
	
	using NodeIndex = uint32_t;
	using NodeTypeId = uint32_t;
	class DLLRTUTIL NodeDesc
		: public std::enable_shared_from_this<NodeDesc>
	{
	public:
		static std::shared_ptr<NodeDesc> Create(GroupNodeDesc *parent);

		NodeDesc(const NodeDesc&)=delete;
		NodeDesc(NodeDesc&&)=delete;
		NodeDesc &operator=(const NodeDesc&)=delete;
		virtual ~NodeDesc()=default;
		std::string GetName() const;
		const std::string &GetTypeName() const;
		std::string ToString() const;
		virtual bool IsGroupNode() const {return false;}

		NodeIndex GetIndex() const;

		operator Socket() const;
		Socket operator-() const {return static_cast<Socket>(*this).operator-();}
		Socket operator+(float f) const {return static_cast<Socket>(*this).operator+(f);}
		Socket operator-(float f) const {return static_cast<Socket>(*this).operator-(f);}
		Socket operator*(float f) const {return static_cast<Socket>(*this).operator*(f);}
		Socket operator/(float f) const {return static_cast<Socket>(*this).operator/(f);}
		Socket operator^(float f) const {return static_cast<Socket>(*this).operator^(f);}
		Socket operator%(float f) const {return static_cast<Socket>(*this).operator%(f);}
		Socket operator<(float f) const {return static_cast<Socket>(*this).operator<(f);}
		Socket operator<=(float f) const {return static_cast<Socket>(*this).operator<=(f);}
		Socket operator>(float f) const {return static_cast<Socket>(*this).operator>(f);}
		Socket operator>=(float f) const {return static_cast<Socket>(*this).operator>=(f);}
		Socket operator+(const Vector3 &v) const {return static_cast<Socket>(*this).operator+(v);}
		Socket operator-(const Vector3 &v) const {return static_cast<Socket>(*this).operator-(v);}
		Socket operator*(const Vector3 &v) const {return static_cast<Socket>(*this).operator*(v);}
		Socket operator/(const Vector3 &v) const {return static_cast<Socket>(*this).operator/(v);}
		Socket operator%(const Vector3 &v) const {return static_cast<Socket>(*this).operator%(v);}
		Socket operator+(const Socket &s) const {return static_cast<Socket>(*this).operator+(s);}
		Socket operator-(const Socket &s) const {return static_cast<Socket>(*this).operator-(s);}
		Socket operator*(const Socket &s) const {return static_cast<Socket>(*this).operator*(s);}
		Socket operator/(const Socket &s) const {return static_cast<Socket>(*this).operator/(s);}
		Socket operator^(const Socket &s) const {return static_cast<Socket>(*this).operator^(s);}
		Socket operator%(const Socket &s) const {return static_cast<Socket>(*this).operator%(s);}
		Socket operator<(const Socket &s) const {return static_cast<Socket>(*this).operator<(s);}
		Socket operator<=(const Socket &s) const {return static_cast<Socket>(*this).operator<=(s);}
		Socket operator>(const Socket &s) const {return static_cast<Socket>(*this).operator>(s);}
		Socket operator>=(const Socket &s) const {return static_cast<Socket>(*this).operator>=(s);}

		template<SocketType type>
			Socket RegisterSocket(const std::string &name,SocketIO io=SocketIO::None)
		{
			assert(io == SocketIO::Out || type == SocketType::Closure);
			return RegisterSocket(name,DataValue{type,nullptr},io);
		}

		template<SocketType type,typename T>
			Socket RegisterSocket(const std::string &name,const T &def,SocketIO io=SocketIO::None)
		{
			if constexpr(std::is_enum_v<T>)
				return RegisterSocket(name,DataValue::Create<std::underlying_type_t<T>,type>(static_cast<std::underlying_type_t<T>>(def)),io);
			else
				return RegisterSocket(name,DataValue::Create<T,type>(def),io);
		}
		Socket RegisterSocket(const std::string &name,const DataValue &value,SocketIO io=SocketIO::None);
		void RegisterPrimaryOutputSocket(const std::string &name);

		template<typename T>
			void SetProperty(const std::string &name,const T &value)
		{
			if constexpr(std::is_enum_v<T>)
				SetProperty<std::underlying_type_t<T>>(name,static_cast<std::underlying_type_t<T>>(value));
			else
				SetProperty<T>(m_properties,name,value);
		}

		template<typename T>
			std::optional<T> GetPropertyValue(const std::string &name) const
		{
			auto it = m_properties.find(name);
			if(it == m_properties.end())
				return {};
			auto &prop = it->second;
			return prop.dataValue.ToValue<T>();
		}

		virtual void SerializeNodes(DataStream &dsOut) const;
		virtual void DeserializeNodes(DataStream &dsIn);
		std::optional<Socket> FindInputSocket(const std::string &name);
		std::optional<Socket> FindOutputSocket(const std::string &name);
		std::optional<Socket> FindProperty(const std::string &name);

		Socket GetInputSocket(const std::string &name);
		Socket GetOutputSocket(const std::string &name);
		Socket GetProperty(const std::string &name);
		Socket GetInputOrProperty(const std::string &name);
		std::optional<Socket> GetPrimaryOutputSocket() const;

		NodeSocketDesc *FindInputSocketDesc(const std::string &name);
		NodeSocketDesc *FindOutputSocketDesc(const std::string &name);
		NodeSocketDesc *FindPropertyDesc(const std::string &name);
		NodeSocketDesc *FindSocketDesc(const Socket &socket);
		NodeSocketDesc *FindInputOrPropertyDesc(const std::string &name);

		GroupNodeDesc *GetParent() const;
		void SetParent(GroupNodeDesc *parent);

		const std::unordered_map<std::string,NodeSocketDesc> &GetInputs() const;
		const std::unordered_map<std::string,NodeSocketDesc> &GetOutputs() const;
		const std::unordered_map<std::string,NodeSocketDesc> &GetProperties() const;
		
		// Internal use only
		void SetTypeName(const std::string &typeName);
	protected:
		template<class TNodeDesc>
			static std::shared_ptr<TNodeDesc> Create(GroupNodeDesc *parent);
		NodeDesc();

		template<typename T>
			void SetProperty(std::unordered_map<std::string,NodeSocketDesc> &properties,const std::string &name,const T &value)
		{
			auto it = properties.find(name);
			if(it == properties.end())
			{
				it = m_inputs.find(name);
				assert(it != m_inputs.end());
				if(it == m_inputs.end())
					throw Exception{"No property named '" +name +"' found for node of type '" +GetTypeName() +"'!"};
			}
			
			auto &prop = it->second;
			it->second.dataValue.value = ToTypeValue<T>(value,prop.dataValue.type);
			assert(it->second.dataValue.value != nullptr);
			if(it->second.dataValue.value == nullptr)
				throw Exception{"Invalid argument type '" +std::string{typeid(value).name()} +"' for property '" +name +"' of type " +to_string(prop.dataValue.type) +"!"};
		}
	private:
		template<typename T>
			std::shared_ptr<void> ToTypeValue(const T &v,SocketType type)
		{
			if constexpr(std::is_same_v<T,EulerAngles>)
				return ToTypeValue(Vector3{umath::deg_to_rad(v.p),umath::deg_to_rad(v.y),umath::deg_to_rad(v.r)},type);
			else if constexpr(std::is_same_v<T,umath::Transform> || std::is_same_v<T,umath::ScaledTransform>)
				return ToTypeValue(Mat4x3{v.ToMatrix()});
			switch(type)
			{
			case SocketType::Bool:
			{
				if constexpr(std::is_convertible_v<T,bool>)
					return std::make_shared<bool>(static_cast<bool>(v));
				return nullptr;
			}
			case SocketType::Float:
			{
				if constexpr(std::is_convertible_v<T,float>)
					return std::make_shared<float>(static_cast<float>(v));
				return nullptr;
			}
			case SocketType::Int:
			case SocketType::Enum:
			{
				if constexpr(std::is_convertible_v<T,int32_t>)
					return std::make_shared<int32_t>(static_cast<int32_t>(v));
				return nullptr;
			}
			case SocketType::UInt:
			{
				if constexpr(std::is_convertible_v<T,uint32_t>)
					return std::make_shared<uint32_t>(static_cast<uint32_t>(v));
				return nullptr;
			}
			case SocketType::Color:
			case SocketType::Vector:
			case SocketType::Point:
			case SocketType::Normal:
			{
				if constexpr(std::is_convertible_v<T,Vector3>)
					return std::make_shared<Vector3>(static_cast<Vector3>(v));
				return nullptr;
			}
			case SocketType::Point2:
			{
				if constexpr(std::is_convertible_v<T,Vector2>)
					return std::make_shared<Vector2>(static_cast<Vector2>(v));
				return nullptr;
			}
			case SocketType::String:
			{
				if constexpr(std::is_convertible_v<T,std::string>)
					return std::make_shared<std::string>(static_cast<std::string>(v));
				return nullptr;
			}
			case SocketType::Transform:
			{
				if constexpr(std::is_convertible_v<T,Mat4x3>)
					return std::make_shared<Mat4x3>(static_cast<Mat4x3>(v));
				return nullptr;
			}
			case SocketType::FloatArray:
			{
				if constexpr(std::is_convertible_v<T,std::vector<float>>)
					return std::make_shared<std::vector<float>>(static_cast<std::vector<float>>(v));
				return nullptr;
			}
			case SocketType::ColorArray:
			{
				if constexpr(std::is_convertible_v<T,std::vector<Vector3>>)
					return std::make_shared<std::vector<Vector3>>(static_cast<std::vector<Vector3>>(v));
				return nullptr;
			}
			}
			static_assert(umath::to_integral(SocketType::Count) == 16);
			return nullptr;
		}
		std::string m_typeName;
		std::string m_name;
		std::unordered_map<std::string,NodeSocketDesc> m_inputs;
		std::unordered_map<std::string,NodeSocketDesc> m_outputs;
		std::unordered_map<std::string,NodeSocketDesc> m_properties;
		std::optional<std::string> m_primaryOutputSocket {};
		std::weak_ptr<GroupNodeDesc> m_parent {};
	};

	enum class TextureType : uint8_t
	{
		EquirectangularImage,
		ColorImage,
		NonColorImage,
		NormalMap,
		Count
	};

	class NodeManager;
	class DLLRTUTIL GroupNodeDesc
		: public NodeDesc
	{
	public:
		static std::shared_ptr<GroupNodeDesc> Create(NodeManager &nodeManager,GroupNodeDesc *parent=nullptr);
		const std::vector<std::shared_ptr<NodeDesc>> &GetChildNodes() const;
		const std::vector<NodeDescLink> &GetLinks() const;
		virtual bool IsGroupNode() const override {return true;}

		NodeDesc *FindNode(const std::string &name);
		NodeDesc *FindNodeByType(const std::string &type);
		std::optional<NodeIndex> FindNodeIndex(NodeDesc &node) const;
		NodeDesc *GetNodeByIndex(NodeIndex idx) const;

		void ResolveGroupNodes();
		NodeDesc &AddNode(const std::string &typeName);
		NodeDesc &AddNode(NodeTypeId id);
		Socket AddMathNode(const Socket &socket0,const Socket &socket1,nodes::math::MathType mathOp);
		NodeDesc &AddVectorMathNode(const Socket &socket0,const Socket &socket1,nodes::vector_math::MathType mathOp);
		Socket CombineRGB(const Socket &r,const Socket &g,const Socket &b);
		NodeDesc &SeparateRGB(const Socket &rgb);
		NodeDesc &AddImageTextureNode(const std::string &fileName,TextureType type=TextureType::ColorImage);
		NodeDesc &AddImageTextureNode(const Socket &fileNameSocket,TextureType type=TextureType::ColorImage);
		Socket AddNormalMapNode(const std::optional<std::string> &fileName,const std::optional<Socket> &fileNameSocket,float strength=1.f);
		Socket AddConstantNode(float f);
		Socket AddConstantNode(const Vector3 &v);
		Socket Mix(const Socket &socket0,const Socket &socket1,const Socket &fac);
		Socket Mix(const Socket &socket0,const Socket &socket1,const Socket &fac,nodes::mix::Mix type);
		Socket Invert(const Socket &socket,const std::optional<Socket> &fac={});
		Socket ToGrayScale(const Socket &socket);
		void Link(const Socket &fromSocket,const Socket &toSocket);
		void Link(NodeDesc &fromNode,const std::string &fromSocket,NodeDesc &toNode,const std::string &toSocket);
		void Serialize(DataStream &dsOut);
		void Deserialize(DataStream &dsOut);
	protected:
		virtual void SerializeNodes(DataStream &dsOut) const override;
		void SerializeLinks(DataStream &dsOut,const std::unordered_map<const NodeDesc*,uint64_t> &nodeIndexTable);

		virtual void DeserializeNodes(DataStream &dsIn) override;
		void DeserializeLinks(DataStream &dsIn,const std::vector<const NodeDesc*> &nodeIndexTable);
		
		std::vector<std::shared_ptr<unirender::NodeDesc>>::iterator ResolveGroupNodes(std::vector<std::shared_ptr<unirender::NodeDesc>>::iterator itParent);
		unirender::NodeDesc &AddNormalMapNodeDesc(const std::optional<std::string> &fileName,const std::optional<Socket> &fileNameSocket,float strength=1.f);
		unirender::NodeDesc &AddImageTextureNode(const std::optional<std::string> &fileName,const std::optional<Socket> &fileNameSocket,TextureType type);
		GroupNodeDesc(NodeManager &nodeManager);
	private:
		std::vector<std::shared_ptr<NodeDesc>> m_nodes = {};
		std::vector<NodeDescLink> m_links = {};
		NodeManager &m_nodeManager;
	};

	class DLLRTUTIL Shader final
		: public std::enable_shared_from_this<Shader>,
		public BaseObject
	{
	public:
		enum class Pass : uint8_t
		{
			Combined = 0,
			Albedo,
			Normal,
			Depth
		};
		template<class TShader>
			static std::shared_ptr<TShader> Create()
		{
			auto shader = std::shared_ptr<TShader>{new TShader{}};
			shader->Initialize();
			return shader;
		}
		~Shader()=default;

		void SetActivePass(Pass pass);
		std::shared_ptr<unirender::GroupNodeDesc> GetActivePassNode() const;

		void Serialize(DataStream &dsOut) const;
		void Deserialize(DataStream &dsIn,NodeManager &nodeManager);

		const std::optional<util::HairConfig> &GetHairConfig() const {return m_hairConfig;}
		void SetHairConfig(const util::HairConfig &hairConfig) {m_hairConfig = hairConfig;}
		void ClearHairConfig() {m_hairConfig = {};}

		std::shared_ptr<unirender::GroupNodeDesc> combinedPass = nullptr;
		std::shared_ptr<unirender::GroupNodeDesc> albedoPass = nullptr;
		std::shared_ptr<unirender::GroupNodeDesc> normalPass = nullptr;
		std::shared_ptr<unirender::GroupNodeDesc> depthPass = nullptr;

		void Finalize();
	protected:
		void Initialize();
	private:
		Shader();
		Pass m_activePass = Pass::Combined;
		std::optional<util::HairConfig> m_hairConfig {};
	};

	using GenericShader = Shader;

	struct DLLRTUTIL NodeType
	{
		std::string typeName;
		std::function<std::shared_ptr<NodeDesc>(GroupNodeDesc*)> factory = nullptr;
	};
	class DLLRTUTIL NodeManager
		: public std::enable_shared_from_this<NodeManager>
	{
	public:
		static std::shared_ptr<NodeManager> Create();
		NodeTypeId RegisterNodeType(const std::string &typeName,const std::function<std::shared_ptr<NodeDesc>(GroupNodeDesc*)> &factory);
		std::optional<NodeTypeId> FindNodeTypeId(const std::string &typeName) const;

		template<typename TNode>
			NodeTypeId RegisterNodeType(const std::string &typeName)
		{
			return RegisterNodeType(typeName,[]() -> std::shared_ptr<Node> {return std::shared_ptr<TNode>{new TNode{}};});
		}

		void RegisterNodeTypes();
		std::shared_ptr<NodeDesc> CreateNode(const std::string &typeName,GroupNodeDesc *parent=nullptr) const;
		std::shared_ptr<NodeDesc> CreateNode(NodeTypeId id,GroupNodeDesc *parent=nullptr) const;
	private:
		NodeManager()=default;
		std::vector<NodeType> m_nodeTypes;
	};

	// TODO: Change these to std::string once C++20 is properly supported by Visual Studio
	constexpr auto *NODE_MATH = "math";
	constexpr auto *NODE_HSV = "hsv";
	constexpr auto *NODE_SEPARATE_XYZ = "separate_xyz";
	constexpr auto *NODE_COMBINE_XYZ = "combine_xyz";
	constexpr auto *NODE_SEPARATE_RGB = "separate_rgb";
	constexpr auto *NODE_COMBINE_RGB = "combine_rgb";
	constexpr auto *NODE_GEOMETRY = "geometry";
	constexpr auto *NODE_CAMERA_INFO = "camera_info";
	constexpr auto *NODE_IMAGE_TEXTURE = "image_texture";
	constexpr auto *NODE_NORMAL_TEXTURE = "normal_texture";
	constexpr auto *NODE_ENVIRONMENT_TEXTURE = "environment_texture";
	constexpr auto *NODE_MIX_CLOSURE = "mix_closure";
	constexpr auto *NODE_ADD_CLOSURE = "add_closure";
	constexpr auto *NODE_BACKGROUND_SHADER = "background_shader";
	constexpr auto *NODE_TEXTURE_COORDINATE = "texture_coordinate";
	constexpr auto *NODE_MAPPING = "mapping";
	constexpr auto *NODE_SCATTER_VOLUME = "scatter_volume";
	constexpr auto *NODE_EMISSION = "emission";
	constexpr auto *NODE_COLOR = "color";
	constexpr auto *NODE_ATTRIBUTE = "attribute";
	constexpr auto *NODE_LIGHT_PATH = "light_path";
	constexpr auto *NODE_TRANSPARENT_BSDF = "transparent_bsdf";
	constexpr auto *NODE_TRANSLUCENT_BSDF = "translucent_bsdf";
	constexpr auto *NODE_DIFFUSE_BSDF = "diffuse_bsdf";
	constexpr auto *NODE_NORMAL_MAP = "normal_map";
	constexpr auto *NODE_PRINCIPLED_BSDF = "principled_bsdf";
	constexpr auto *NODE_TOON_BSDF = "toon_bsdf";
	constexpr auto *NODE_GLASS_BSDF = "glass_bsdf";
	constexpr auto *NODE_OUTPUT = "output";
	constexpr auto *NODE_VECTOR_MATH = "vector_math";
	constexpr auto *NODE_MIX = "mix";
	constexpr auto *NODE_RGB_TO_BW = "rgb_to_bw";
	constexpr auto *NODE_INVERT = "invert";
	constexpr auto *NODE_VECTOR_TRANSFORM = "vector_transform";
	constexpr auto *NODE_RGB_RAMP = "rgb_ramp";
	constexpr auto *NODE_LAYER_WEIGHT = "layer_weight";
	static_assert(NODE_COUNT == 36,"Increase this number if new node types are added!");
};

DLLRTUTIL std::ostream& operator<<(std::ostream &os,const unirender::NodeDesc &desc);
DLLRTUTIL std::ostream& operator<<(std::ostream &os,const unirender::GroupNodeDesc &desc);

#endif
