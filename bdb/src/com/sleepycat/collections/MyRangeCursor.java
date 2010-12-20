/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2000,2008 Oracle.  All rights reserved.
 *
 * $Id: MyRangeCursor.java,v 1.8 2008/02/05 23:28:19 mark Exp $
 */

package com.sleepycat.collections;

import com.sleepycat.compat.DbCompat;
import com.sleepycat.je.Cursor;
import com.sleepycat.je.CursorConfig;
import com.sleepycat.je.DatabaseException;
import com.sleepycat.util.keyrange.KeyRange;
import com.sleepycat.util.keyrange.RangeCursor;

class MyRangeCursor extends RangeCursor {

    private DataView view;
    private boolean isRecnoOrQueue;
    private boolean writeCursor;

    MyRangeCursor(KeyRange range,
                  CursorConfig config,
                  DataView view,
                  boolean writeAllowed)
        throws DatabaseException {

        super(range, view.dupsRange, view.dupsOrdered,
              openCursor(view, config, writeAllowed));
        this.view = view;
        isRecnoOrQueue = view.recNumAllowed && !view.btreeRecNumDb;
        writeCursor = isWriteCursor(config, writeAllowed);
    }

    /**
     * Returns true if a write cursor is requested by the user via the cursor
     * config, or if this is a writable cursor and the user has not specified a
     * cursor config.  For CDB, a special cursor must be created for writing.
     * See CurrentTransaction.openCursor.
     */
    private static boolean isWriteCursor(CursorConfig config,
                                         boolean writeAllowed) {
        return DbCompat.getWriteCursor(config) ||
               (config == CursorConfig.DEFAULT && writeAllowed);
    }

    private static Cursor openCursor(DataView view,
                                     CursorConfig config,
                                     boolean writeAllowed)
        throws DatabaseException {

        return view.currentTxn.openCursor
            (view.db, config, isWriteCursor(config, writeAllowed),
             view.useTransaction());
    }

    protected Cursor dupCursor(Cursor cursor, boolean samePosition)
        throws DatabaseException {

        return view.currentTxn.dupCursor(cursor, writeCursor, samePosition);
    }

    protected void closeCursor(Cursor cursor)
        throws DatabaseException {

        view.currentTxn.closeCursor(cursor);
    }

    protected boolean checkRecordNumber() {
        return isRecnoOrQueue;
    }
}