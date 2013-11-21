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


import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.configCommitCount;
import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.configDimCount;
import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.configDimPerFact;

import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.util.Random;

import org.apache.log4j.Logger;

import com.oltpbenchmark.api.Loader;
import com.oltpbenchmark.benchmarks.micro.jdbc.jdbcIO;
import com.oltpbenchmark.benchmarks.micro.pojo.Dim;
import com.oltpbenchmark.benchmarks.micro.pojo.Fact;
import com.oltpbenchmark.catalog.Table;
import com.oltpbenchmark.util.SQLUtil;

public class MicroLoader extends Loader{
    private static final Logger LOG = Logger.getLogger(MicroLoader.class);

	public MicroLoader(MicroBenchmark benchmark, Connection c) {
		super(benchmark, c);
        outputFiles= false;
	sf = (int)Math.round(this.scaleFactor);
	}

	static boolean fastLoad;
	static String fastLoaderBaseDir;

	// ********** general vars **********************************
	private static java.util.Date now = null;
	private static java.util.Date startDate = null;
	private static java.util.Date endDate = null;

	private static Random gen;
	private static int sf = 0; 
	private static String fileLocation = "";
	private static boolean outputFiles = false;
	private static PrintWriter out = null;
	private static long lastTimeMS = 0;

	private static final int FIRST_UNPROCESSED_O_ID = 2101;
	
	private PreparedStatement getInsertStatement(String tableName) throws SQLException {
        Table catalog_tbl = this.getTableCatalog(tableName);
        assert(catalog_tbl != null);
        String sql = SQLUtil.getInsertSQL(catalog_tbl);
        PreparedStatement stmt = this.conn.prepareStatement(sql);
        return stmt;
	}

	protected void transRollback() {
		if (outputFiles == false) {
			try {
				conn.rollback();
			} catch (SQLException se) {
				LOG.debug(se.getMessage());
			}
		} else {
			out.close();
		}
	}

	protected void transCommit() {
		if (outputFiles == false) {
			try {
				conn.commit();
			} catch (SQLException se) {
				LOG.debug(se.getMessage());
				transRollback();
			}
		} else {
			out.close();
		}
	}

	protected void truncateTable(String strTable) {

		LOG.debug("Truncating '" + strTable + "' ...");
		try {
			this.conn.createStatement().execute("TRUNCATE TABLE " + strTable);
			transCommit();
		} catch (SQLException se) {
			LOG.debug(se.getMessage());
			transRollback();
		}

	}

	protected int loadDim(int dimKount) {

		int k = 0;
		int t = 0;

		try {
		    PreparedStatement dimPrepStmt = getInsertStatement(MicroConstants.TABLENAME_DIM);

		    now = new java.util.Date();
		    t = dimKount;
			if (outputFiles == true) {
				out = new PrintWriter(new FileOutputStream(fileLocation
						+ "item.csv"));
				LOG.debug("\nWriting Item file to: " + fileLocation
						+ "item.csv");
			}

			Dim dim = new Dim();

			for (int i = 1; i <= dimKount; i++) {

				dim.d_id = i;
				dim.d_value = (MicroUtil.randomNumber(1, 10000, gen));
				k++;

				if (outputFiles == false) {
					dimPrepStmt.setLong(1, dim.d_id);
					dimPrepStmt.setLong(2, dim.d_value);
					dimPrepStmt.addBatch();

					if ((k % configCommitCount) == 0) {

						long tmpTime = new java.util.Date().getTime();

						String etStr = "  Elasped Time(ms): "
								+ ((tmpTime - lastTimeMS) / 1000.000)
								+ "                    ";
						LOG.debug(etStr.substring(0, 30)
								+ "  Writing record " + k + " of " + t);
						lastTimeMS = tmpTime;
						dimPrepStmt.executeBatch();
						dimPrepStmt.clearBatch();
						transCommit();
					}
				} else {
					String str = "";
					str = str + dim.d_id + ",";
					str = str + dim.d_value; 
					out.println(str);

					if ((k % configCommitCount) == 0) {
						long tmpTime = new java.util.Date().getTime();
						String etStr = "  Elasped Time(ms): "
								+ ((tmpTime - lastTimeMS) / 1000.000)
								+ "                    ";
						LOG.debug(etStr.substring(0, 30)
								+ "  Writing record " + k + " of " + t);
						lastTimeMS = tmpTime;
					}
				}

			} 

			long tmpTime = new java.util.Date().getTime();
			String etStr = "  Elasped Time(ms): "
					+ ((tmpTime - lastTimeMS) / 1000.000)
					+ "                    ";
			LOG.debug(etStr.substring(0, 30) + "  Writing record " + k
					+ " of " + t);
			lastTimeMS = tmpTime;

			if (outputFiles == false) {
				dimPrepStmt.executeBatch();
			}

			transCommit();
			now = new java.util.Date();
			LOG.debug("End Item Load @  " + now);

		} catch (SQLException se) {
			LOG.debug(se.getMessage());
			transRollback();
		} catch (Exception e) {
			e.printStackTrace();
			transRollback();
		}

		return (k);

	}

	protected int loadFact(int dimKount, int factKount) {

		int k = 0;
		int t = 0;

		try {
		    PreparedStatement factPrepStmt = getInsertStatement(MicroConstants.TABLENAME_FACT);

			now = new java.util.Date();
			t = factKount;
			LOG.error("\nStart Fact Load for " + factKount + " Facts @ " + now
					+ " ...");

			if (outputFiles == true) {
				out = new PrintWriter(new FileOutputStream(fileLocation
						+ "fact.csv"));
				LOG.debug("\nWriting Item file to: " + fileLocation
						+ "fact.csv");
			}

			Fact fact = new Fact();

			for (int i = 1; i <= factKount; i++) {

				fact.f_id = i;
				fact.f_d_id = (MicroUtil.randomNumber(1, dimKount, gen));
				k++;

				if (outputFiles == false) {
					factPrepStmt.setLong(1, fact.f_id);
					factPrepStmt.setLong(2, fact.f_d_id);
					factPrepStmt.addBatch();

					if ((k % configCommitCount) == 0) {
						long tmpTime = new java.util.Date().getTime();
						String etStr = "  Elasped Time(ms): "
								+ ((tmpTime - lastTimeMS) / 1000.000)
								+ "                    ";
						LOG.debug(etStr.substring(0, 30)
								+ "  Writing record " + k + " of " + t);
						lastTimeMS = tmpTime;
						factPrepStmt.executeBatch();
						factPrepStmt.clearBatch();
						transCommit();
					}
				} else {
					String str = "";
					str = str + fact.f_id + ",";
					str = str + fact.f_d_id; 
					out.println(str);

					if ((k % configCommitCount) == 0) {
						long tmpTime = new java.util.Date().getTime();
						String etStr = "  Elasped Time(ms): "
								+ ((tmpTime - lastTimeMS) / 1000.000)
								+ "                    ";
						LOG.debug(etStr.substring(0, 30)
								+ "  Writing record " + k + " of " + t);
						lastTimeMS = tmpTime;
					}
				}

			} 

			long tmpTime = new java.util.Date().getTime();
			String etStr = "  Elasped Time(ms): "
					+ ((tmpTime - lastTimeMS) / 1000.000)
					+ "                    ";
			LOG.debug(etStr.substring(0, 30) + "  Writing record " + k
					+ " of " + t);
			lastTimeMS = tmpTime;

			if (outputFiles == false) {
				factPrepStmt.executeBatch();
			}

			transCommit();
			now = new java.util.Date();
			LOG.debug("End Item Load @  " + now);

		} catch (SQLException se) {
			LOG.debug(se.getMessage());
			transRollback();
		} catch (Exception e) {
			e.printStackTrace();
			transRollback();
		}

		return (k);

	}



	public static final class NotImplementedException extends
			UnsupportedOperationException {

        private static final long serialVersionUID = 1958656852398867984L;
	}

	@Override
	public void load() throws SQLException {

		if (outputFiles == false) {
			truncateTable(MicroConstants.TABLENAME_DIM);
			truncateTable(MicroConstants.TABLENAME_FACT);
		}

		gen = new Random(System.currentTimeMillis());

		startDate = new java.util.Date();
		LOG.debug("------------- LoadData Start Date = " + startDate
				+ "-------------");

		long startTimeMS = new java.util.Date().getTime();
		lastTimeMS = startTimeMS;
		int dimCount = sf * configDimCount;
		int factCount = dimCount * configDimPerFact;

		long totalRows = loadFact(dimCount, factCount);
		totalRows += loadDim(dimCount);

	
	}
} 
