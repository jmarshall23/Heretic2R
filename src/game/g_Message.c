//
// g_Message.c
//
// Copyright 1998 Raven Software
//

#include "g_Message.h"
#include "Message.h"
#include "g_local.h"
#include "ResourceManager.h"
#include "SinglyLinkedList.h"

#include <assert.h>
#include <stdarg.h>

static ResourceManager_t g_messages_manager; // mxd. Named 'messages_manager' in original logic.

void G_InitMsgMngr(void) // mxd. Named 'InitMsgMngr' in original logic.
{
#define MESSAGE_BLOCK_SIZE 256
	ResMngr_Con(&g_messages_manager, sizeof(G_Message_t), MESSAGE_BLOCK_SIZE);
}

void G_ReleaseMsgMngr(void) // mxd. Named 'ReleaseMsgMngr' in original logic.
{
	ResMngr_Des(&g_messages_manager);
}

static void G_Message_DefaultCon(G_Message_t* self)
{
	assert(self);

	SinglyLinkedList_t* parms = &self->parms;

	// Need to port object manager to C.
	SLList_DefaultCon(parms);

	// Keep one empty parm node so MSG_SetParms / MSG_GetParms logic has a current node.
	SLList_PushEmpty(parms);
}

G_Message_t* G_Message_New(const G_MsgID_t id, const G_MsgPriority_t priority)
{
	G_Message_t* msg = (G_Message_t*)ResMngr_AllocateResource(&g_messages_manager, sizeof(G_Message_t));
	assert(msg);

	G_Message_DefaultCon(msg);
	msg->ID = id;
	msg->priority = priority;

	return msg;
}

static void G_Message_Des(G_Message_t* self)
{
	assert(self);
	SLList_Des(&self->parms);
}

void G_Message_Delete(G_Message_t* msg)
{
	if (msg == NULL)
		return;

	G_Message_Des(msg);
	ResMngr_DeallocateResource(&g_messages_manager, msg, sizeof(G_Message_t));
}

void G_PostMessage(edict_t* to, const G_MsgID_t id, const G_MsgPriority_t priority, const char* format, ...) // mxd. Named 'QPostMessage' in original logic.
{
	if (to == NULL)
		return;

	// Everything should really have one, but at this point everything doesn't.
	// So, the messages will never get popped off the queue, so don't push them on in the first place.
	if (to->msgHandler == NULL)
		return;

	G_Message_t* msg = G_Message_New(id, priority);
	assert(msg);

	if (format != NULL)
	{
		va_list marker;
		va_start(marker, format);
		MSG_SetParms(&msg->parms, format, marker);
		va_end(marker);
	}

	MSG_Queue(&to->msgQ, msg);
}

int G_ParseMsgParms(G_Message_t* msg, char* format, ...) // mxd. Named 'ParseMsgParms' in original logic.
{
	assert(msg != NULL);
	assert(format != NULL);

	SinglyLinkedList_t* parms = &msg->parms;
	SLList_Front(parms);

	va_list marker;
	va_start(marker, format);
	const int args_filled = MSG_GetParms(parms, format, marker);
	va_end(marker);

	return args_filled;
}

void G_ProcessMessages(edict_t* self) // mxd. Named 'ProcessMessages' in original logic.
{
	assert(self != NULL);
	assert(self->msgHandler != NULL);

	SinglyLinkedList_t* msgs = &self->msgQ.msgs;

	if (!SLList_IsEmpty(msgs))
		self->flags &= ~FL_SUSPENDED;

	while (!SLList_IsEmpty(msgs))
	{
		G_Message_t* msg = (G_Message_t*)SLList_Pop(msgs).t_void_p;
		assert(msg != NULL);

		SinglyLinkedList_t* parms = &msg->parms;

		// Trim any leftover parameter nodes beyond the current/active portion.
		if (!SLList_AtLast(parms) && !SLList_AtEnd(parms))
			SLList_Chop(parms);

		self->msgHandler(self, msg);

		G_Message_Delete(msg);
	}
}

static void G_ClearMessageQueue(edict_t* self) // mxd. Named 'ClearMessageQueue' in original logic.
{
	if (self == NULL)
		return;

	SinglyLinkedList_t* msgs = &self->msgQ.msgs;

	while (!SLList_IsEmpty(msgs))
	{
		G_Message_t* msg = (G_Message_t*)SLList_Pop(msgs).t_void_p;
		if (msg != NULL)
			G_Message_Delete(msg);
	}
}

void G_ClearMessageQueues(void) // mxd. Named 'ClearMessageQueues' in original logic.
{
	edict_t* ent = &g_edicts[0];

	for (int i = 0; i < globals.num_edicts; ++i, ++ent)
		G_ClearMessageQueue(ent);
}