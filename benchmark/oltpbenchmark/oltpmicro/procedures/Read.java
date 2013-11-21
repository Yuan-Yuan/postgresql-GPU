package com.oltpbenchmark.benchmarks.micro.procedures;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.util.ArrayList;
import java.util.Random;

import org.apache.log4j.Logger;

import com.oltpbenchmark.api.SQLStmt;
import com.oltpbenchmark.benchmarks.micro.MicroConstants;
import com.oltpbenchmark.benchmarks.micro.MicroUtil;
import com.oltpbenchmark.benchmarks.micro.MicroWorker;
import com.oltpbenchmark.benchmarks.micro.pojo.Dim;
import com.oltpbenchmark.benchmarks.micro.pojo.Fact;

public class Read extends MicroProcedure {

    private static final Logger LOG = Logger.getLogger(Read.class);
	
	public SQLStmt scanFactSQL = new SQLStmt("SELECT f_id FROM " + MicroConstants.TABLENAME_FACT
			+ ";");

	private PreparedStatement scanFact = null;


	 public ResultSet run(Connection conn, Random gen, int scaleFactor, 
				MicroWorker w) throws SQLException{
	
		 
			scanFact =this.getPreparedStatement(conn, scanFactSQL);
				
			int y = MicroUtil.randomNumber(1, 100, gen);

			readTransaction(conn, w);
			return null;
	 }
	
	private void readTransaction(Connection conn, MicroWorker w) throws SQLException {
		int f_id = -1;
		Timestamp entdate;
		ArrayList<String> fact = new ArrayList<String>();

		ResultSet rs = scanFact.executeQuery();

		rs = null;

		conn.commit();
	}
			
}



