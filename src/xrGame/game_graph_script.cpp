////////////////////////////////////////////////////////////////////////////
//	Module 		: game_graph_script.cpp
//	Created 	: 02.11.2005
//  Modified 	: 02.11.2005
//	Author		: Dmitriy Iassenev
//	Description : Game graph class script export
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <luabind/luabind.hpp>
#include <luabind/iterator_policy.hpp>
#include "game_graph.h"
#include "ai_space.h"

using namespace luabind;

const IGameGraph *get_game_graph		()
{
	return				(&ai().game_graph());
}

const IGameGraph::CHeader *get_header	(const IGameGraph *self)
{
	return				(&self->header());
}

bool get_accessible1					(const IGameGraph *self, const u32 &vertex_id)
{
	return				(self->accessible(vertex_id));
}

void get_accessible2					(const IGameGraph *self, const u32 &vertex_id, bool value)
{
	self->accessible	(vertex_id,value);
}

Fvector CVertex__level_point			(const IGameGraph::CVertex *vertex)
{
	THROW				(vertex);
	return				(vertex->level_point());
}

Fvector CVertex__game_point				(const IGameGraph::CVertex *vertex)
{
	THROW				(vertex);
	return				(vertex->game_point());
}

GameGraph::LEVEL_MAP const& get_levels	( IGameGraph const* graph )
{
	THROW				(graph);
	return				graph->header().levels();
}

namespace GameGraphScript
{
	static s32 GetVertexLocationType(const IGameGraph::CVertex* Vertex, const luabind::object& Index)
	{
		constexpr s32 InvalidLocationType = -1;
		if (Vertex == nullptr || !Index.is_valid() || Index.type() != LUA_TNUMBER)
		{
			return InvalidLocationType;
		}

		const double Value = luabind::object_cast_nothrow<double>(Index).value_or(InvalidLocationType);
		// Reject subnormal values before floating-point comparisons can treat them as zero.
		if (!_valid(Value) || Value < 0 || Value >= (double)GameGraph::LOCATION_TYPE_COUNT)
		{
			return InvalidLocationType;
		}

		const u32 ComponentIndex = (u32)Value;
		if (Value != ComponentIndex)
		{
			return InvalidLocationType;
		}

		return Vertex->vertex_type()[ComponentIndex];
	}
} // namespace GameGraphScript

#pragma optimize("s",on)
void CGameGraph::script_register		(lua_State *L)
{
	module(L)
	[
		class_< GameGraph::LEVEL_MAP::value_type >( "GameGraph__LEVEL_MAP__value_type" )
		.def_readonly("id", 	&GameGraph::LEVEL_MAP::value_type::first )
		.def_readonly("level",	&GameGraph::LEVEL_MAP::value_type::second ),

		def("game_graph",	&get_game_graph),

		class_<IGameGraph>("CGameGraph")
			.def("accessible",		&get_accessible1)
			.def("accessible",		&get_accessible2)
			.def("valid_vertex_id",	&IGameGraph::valid_vertex_id)
			.def("vertex",			&IGameGraph::vertex)
			.def("vertex_id",		&IGameGraph::vertex_id)
			.def("levels",			&get_levels, return_stl_iterator),

		class_<CVertex>("GameGraph__CVertex")
			.def("level_point",		&CVertex__level_point)
			.def("game_point",		&CVertex__game_point)
			.def("level_id",		&CVertex::level_id)
			.def("level_vertex_id",	&CVertex::level_vertex_id)
			.def("LocationType", &GameGraphScript::GetVertexLocationType)
	];
}

