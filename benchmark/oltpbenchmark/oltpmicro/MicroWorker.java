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
 * jTPCCTerminal - Terminal emulator code for jTPCC (transactions)
 *
 * Copyright (C) 2003, Raul Barbosa
 * Copyright (C) 2004-2006, Denis Lussier
 *
 */

import java.sql.SQLException;
import java.util.Random;
import java.util.concurrent.atomic.AtomicInteger;

import com.oltpbenchmark.api.Procedure.UserAbortException;
import com.oltpbenchmark.api.TransactionType;
import com.oltpbenchmark.api.Worker;
import com.oltpbenchmark.benchmarks.micro.procedures.Read;
import com.oltpbenchmark.benchmarks.micro.procedures.Insert;
import com.oltpbenchmark.benchmarks.micro.procedures.Update;
import com.oltpbenchmark.benchmarks.micro.procedures.MicroProcedure;
import com.oltpbenchmark.types.TransactionStatus;
import com.oltpbenchmark.util.SimplePrinter;

public class MicroWorker extends Worker {

	// private TransactionTypes transactionTypes;

	private String terminalName;

	private SimplePrinter terminalOutputArea, errorOutputArea;
	// private boolean debugMessages;
	private final Random gen = new Random();

	private int transactionCount = 1, scaleFactor;

	private static final AtomicInteger terminalId = new AtomicInteger(0);

	public MicroWorker(String terminalName,
			MicroBenchmark benchmarkModule, SimplePrinter terminalOutputArea,
			SimplePrinter errorOutputArea, int scaleFactor)
			throws SQLException {
		super(benchmarkModule, terminalId.getAndIncrement());
		
		this.terminalName = terminalName;

		this.terminalOutputArea = terminalOutputArea;
		this.errorOutputArea = errorOutputArea;
		this.scaleFactor = scaleFactor;
	}

	/**
	 * Executes a single transaction of type transactionType.
	 */
	@Override
    protected TransactionStatus executeWork(TransactionType nextTransaction) throws UserAbortException, SQLException {
        try {
            MicroProcedure proc = (MicroProcedure) this.getProcedure(nextTransaction.getProcedureClass());
            proc.run(conn, gen,  scaleFactor, this);
        } catch (ClassCastException ex){
        	System.err.println("We have been invoked with an INVALID transactionType?!");
        	throw new RuntimeException("Bad transaction type = "+ nextTransaction);
	    } catch (RuntimeException ex) {
	        conn.rollback();
	        return (TransactionStatus.RETRY_DIFFERENT);
	    }
		transactionCount++;
        conn.commit();
        return (TransactionStatus.SUCCESS);
	}

//	/**
//	 * Rolls back the current transaction, then rethrows e if it is not a
//	 * serialization error. Serialization errors are exceptions caused by
//	 * deadlock detection, lock wait timeout, or similar.
//	 * 
//	 * @param e
//	 *            Exception to check if it is a serialization error.
//	 * @throws SQLException
//	 */
//	// Lame deadlock profiling: set this to new HashMap<Integer, Integer>() to
//	// enable.
//	private final HashMap<Integer, Integer> deadlockLocations = null;
//
//	public void rollbackAndHandleError(SQLException e, Connection conn)
//			throws SQLException {
//		conn.rollback();
//
//		// Unfortunately, JDBC provides no standardized way to do this, so we
//		// resort to this ugly hack.
//		boolean isSerialization = false;
//		if (e.getErrorCode() == 1213 && e.getSQLState().equals("40001")) {
//			// MySQL serialization
//			isSerialization = true;
//			assert e.getMessage()
//					.equals("Deadlock found when trying to get lock; try restarting transaction");
//		} else if (e.getErrorCode() == 1205 && e.getSQLState().equals("40001")) {
//			// SQL Server serialization
//			isSerialization = true;
//			assert e.getMessage().equals("Rerun the transaction.");
//		} else if (e.getErrorCode() == 8177 && e.getSQLState().equals("72000")) {
//			// Oracle serialization
//			isSerialization = true;
//			assert e.getMessage().equals("Rerun the transaction.");
//		} else if (e.getErrorCode() == 0 && e.getSQLState().equals("40001")) {
//			// Postgres serialization
//			isSerialization = true;
//			assert e.getMessage().equals(
//					"could not serialize access due to concurrent update");
//		} else if (e.getErrorCode() == 1205 && e.getSQLState().equals("41000")) {
//			// TODO: This probably shouldn't really happen?
//			// FIXME: What is this?
//			isSerialization = true;
//			assert e.getMessage().equals(
//					"Lock wait timeout exceeded; try restarting transaction");
//		}
//
//		// Djellel
//		// This is to prevent other errors to kill the thread.
//		// Errors may include -- duplicate key
//		if (!isSerialization) {
//			error("Oops SQLException code " + e.getErrorCode() + " state "
//					+ e.getSQLState() + " message: " + e.getMessage());
//			// throw e; //Otherwise the benchmark will keep going
//		}
//
//		if (deadlockLocations != null) {
//			String className = this.getClass().getCanonicalName();
//			for (StackTraceElement trace : e.getStackTrace()) {
//				if (trace.getClassName().equals(className)) {
//					int line = trace.getLineNumber();
//					Integer count = deadlockLocations.get(line);
//					if (count == null)
//						count = 0;
//
//					count += 1;
//					deadlockLocations.put(line, count);
//					return;
//				}
//			}
//			assert false;
//		}
//	}
//
//	PreparedStatement customerByName;
//	boolean isCustomerByName = false;
//
//	private void error(String type) {
//		errorOutputArea.println("[ERROR] TERMINAL=" + terminalName + "  TYPE="
//				+ type + "  COUNT=" + transactionCount);
//	}
//
//
//	public Connection getConnection() {
//		return conn;
//	}
}
