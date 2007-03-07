/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2002,2006 Oracle.  All rights reserved.
 *
 * $Id: LogException.java,v 1.14 2006/10/30 21:14:20 bostic Exp $
 */

package com.sleepycat.je.log;

import com.sleepycat.je.DatabaseException;

/**
 * Configuration related exceptions.
 */
public class LogException extends DatabaseException {
    public LogException(String message) {
	super(message);
    }

    public LogException(String message, Exception e) {
	super(message, e);
    }
}
