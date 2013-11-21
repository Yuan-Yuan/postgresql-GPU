/*******************************************************************************
 * oltpbenchmark.com
 *  
 *  Project Info:  http://oltpbenchmark.com
 *  Project Members:  	Carlo Curino <carlo.curino@gmail.com>
 * 				Evan Jones <ej@evanjones.ca>
 * 				DIFALLAH Djellel Eddine <djelleleddine.difallah@unifr.ch>
 * 				Andy Pavlo <pavlo@cs.brown.edu>
 * 				CUDRE-MAUROUX Philippe <philippe.cudre-mauroux@unifr.ch>  
 *  				Yang Zhang <yaaang@gmail.com> 
 * 
 *  This library is free software; you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation;
 *  either version 3.0 of the License, or (at your option) any later version.
 * 
 *  This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU Lesser General Public License for more details.
 ******************************************************************************/
package com.oltpbenchmark.benchmarks.micro;

/*
 * jMicroConfig - Basic configuration parameters for jMicro
 */

import java.text.SimpleDateFormat;

public final class jMicroConfig {

	public static boolean TERMINAL_MESSAGES = true;

	public static enum TransactionType {
		INVALID, 
		READ, INSERT, UPDATE
	}

	// TODO: Remove these constants
	public final static int  READ= 1, INSERT = 2, UPDATE = 3;

	public final static String[] nameTokens = { "BAR", "OUGHT", "ABLE", "PRI",
			"PRES", "ESE", "ANTI", "CALLY", "ATION", "EING" };

	public final static String terminalPrefix = "Term-";
	public final static String reportFilePrefix = "reports/BenchmarkSQL_session_";

	public final static SimpleDateFormat dateFormat = new SimpleDateFormat(
			"yyyy-MM-dd HH:mm:ss");

	public final static int configCommitCount = 1000; // commit every n records
	public final static int configDimCount = 1024*1024*4; // default number of tuples in Dim table
	public final static int configDimPerFact = 16; // Fact:Dim = 1:16 
	public final static int configTuplePerTx = 10; // the number tuples updated/inserted in one transaction.

	/** An invalid item id used to rollback a new order transaction. */
	public static final int INVALID_ITEM_ID = -12345;
}
