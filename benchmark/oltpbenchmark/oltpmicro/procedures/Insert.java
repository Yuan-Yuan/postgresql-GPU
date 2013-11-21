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
import com.oltpbenchmark.benchmarks.micro.jMicroConfig;
import com.oltpbenchmark.benchmarks.micro.pojo.Fact;

public class Insert extends MicroProcedure {

    private static final Logger LOG = Logger.getLogger(Insert.class);
	
	public SQLStmt insertFactSQL = new SQLStmt("INSERT INTO " + MicroConstants.TABLENAME_FACT + " VALUES (?, ?)");
	
	private PreparedStatement insertFact = null;
	
	 public ResultSet run(Connection conn, Random gen,int scaleFactor, 
				MicroWorker w) throws SQLException{
	
		insertFact=this.getPreparedStatement(conn, insertFactSQL);
		 
            	int factID = MicroUtil.randomNumber(1,1000, gen);
            	int factValue = MicroUtil.randomNumber(1,1000, gen);
        
		insertTransaction(factID, factValue,conn, w);
		return null;
	}
	 
    private void insertTransaction(int f_id, int f_value, Connection conn, MicroWorker w)
				throws SQLException {
		
			insertFact.setInt(1, f_id);
			insertFact.setInt(2, f_value);

			insertFact.executeUpdate();
	
			conn.commit();

		} 
}
