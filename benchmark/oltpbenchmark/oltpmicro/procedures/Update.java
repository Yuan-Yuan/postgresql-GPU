package com.oltpbenchmark.benchmarks.micro.procedures;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.util.Random;

import org.apache.log4j.Logger;

import com.oltpbenchmark.api.SQLStmt;
import com.oltpbenchmark.benchmarks.micro.MicroConstants;
import com.oltpbenchmark.benchmarks.micro.MicroUtil;
import com.oltpbenchmark.benchmarks.micro.MicroWorker;
import com.oltpbenchmark.benchmarks.micro.jMicroConfig;

import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.configTuplePerTx;
import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.configDimPerFact;
import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.configDimCount;

public class Update extends MicroProcedure {
    
    private static final Logger LOG = Logger.getLogger(Update.class);

    public final SQLStmt updateFactSQL = new SQLStmt(
    		"UPDATE " + MicroConstants.TABLENAME_FACT 
			+ " SET f_d_id = ? where f_id = ?");
    
	private PreparedStatement updateFact = null; 
    
    public ResultSet run(Connection conn, Random gen,int scaleFactor, 
			MicroWorker w) throws SQLException {
    
	updateFact = this.getPreparedStatement(conn, updateFactSQL);

	int count = configTuplePerTx;
	int max_did = scaleFactor * configDimCount;
	int max_fid = max_did * configDimPerFact;

	updateTransaction(count, max_fid, max_did,gen, conn, w);

	return null;
    }

	private void updateTransaction(int count, int max_fid, int max_did, Random gen, Connection conn, MicroWorker w)
			throws SQLException {

		try
		{
			for(int i=0;i<count;i++){
				int f_id = MicroUtil.randomNumber(1, max_fid, gen);
				int f_value = MicroUtil.randomNumber(1, max_did, gen);

				updateFact.setInt(1, f_id);
				updateFact.setInt(2, f_value);
				updateFact.executeUpdate();
			}

			conn.commit();

		} catch(UserAbortException userEx)
		{
		    LOG.debug("Caught an expected error in New Order");
		    throw userEx;
		}finally {
            		if (updateFact != null)
                		updateFact.clearBatch();
        	}

	}
    
}
