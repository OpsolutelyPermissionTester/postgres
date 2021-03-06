/*-------------------------------------------------------------------------
 *
 * printsimple.c
 *	  Routines to print out tuples containing only a limited range of
 *	  builtin types without catalog access.  This is intended for
 *	  backends that don't have catalog access because they are not bound
 *	  to a specific database, such as some walsender processes.  It
 *	  doesn't handle standalone backends or protocol versions other than
 *	  3.0, because we don't need such handling for current applications.
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/access/common/printsimple.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/printsimple.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "libpq/pqformat.h"

/*
 * At startup time, send a RowDescription message.
 */
void
printsimple_startup(DestReceiver *self, int operation, TupleDesc tupdesc)
{
	StringInfoData buf;
	int			i;

	pq_beginmessage(&buf, 'T'); /* RowDescription */
	pq_sendint(&buf, tupdesc->natts, 2);

	for (i = 0; i < tupdesc->natts; ++i)
	{
		Form_pg_attribute attr = tupdesc->attrs[i];

		pq_sendstring(&buf, NameStr(attr->attname));
		pq_sendint(&buf, 0, 4); /* table oid */
		pq_sendint(&buf, 0, 2); /* attnum */
		pq_sendint(&buf, (int) attr->atttypid, 4);
		pq_sendint(&buf, attr->attlen, 2);
		pq_sendint(&buf, attr->atttypmod, 4);
		pq_sendint(&buf, 0, 2); /* format code */
	}

	pq_endmessage(&buf);
}

/*
 * For each tuple, send a DataRow message.
 */
bool
printsimple(TupleTableSlot *slot, DestReceiver *self)
{
	TupleDesc	tupdesc = slot->tts_tupleDescriptor;
	StringInfoData buf;
	int			i;

	/* Make sure the tuple is fully deconstructed */
	slot_getallattrs(slot);

	/* Prepare and send message */
	pq_beginmessage(&buf, 'D');
	pq_sendint(&buf, tupdesc->natts, 2);

	for (i = 0; i < tupdesc->natts; ++i)
	{
		Form_pg_attribute attr = tupdesc->attrs[i];
		Datum		value;

		if (slot->tts_isnull[i])
		{
			pq_sendint(&buf, -1, 4);
			continue;
		}

		value = slot->tts_values[i];

		/*
		 * We can't call the regular type output functions here because we
		 * might not have catalog access.  Instead, we must hard-wire
		 * knowledge of the required types.
		 */
		switch (attr->atttypid)
		{
			case TEXTOID:
				{
					text	   *t = DatumGetTextPP(value);

					pq_sendcountedtext(&buf,
									   VARDATA_ANY(t),
									   VARSIZE_ANY_EXHDR(t),
									   false);
				}
				break;

			default:
				elog(ERROR, "unsupported type OID: %u", attr->atttypid);
		}
	}

	pq_endmessage(&buf);

	return true;
}
