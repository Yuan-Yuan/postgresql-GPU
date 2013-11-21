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
package com.oltpbenchmark.benchmarks.micro.jdbc;

/*
 * jdbcIO - execute JDBC statements
 *
 * Copyright (C) 2004-2006, Denis Lussier
 *
 */

import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.sql.Types;

import com.oltpbenchmark.benchmarks.micro.pojo.Dim;
import com.oltpbenchmark.benchmarks.micro.pojo.Fact;

public class jdbcIO {

	public void insertDim(PreparedStatement dimPrepStmt, Dim dim) {

		try {

			dimPrepStmt.setInt(1, dim.d_id);
			dimPrepStmt.setInt(2, dim.d_value);
			dimPrepStmt.addBatch();
			
		} catch (SQLException se) {
			throw new RuntimeException(se);
		}

	} 

	public void insertFact(PreparedStatement factPrepStmt,
			Fact fact) {

		try {
			factPrepStmt.setInt(1, fact.f_id);
			factPrepStmt.setInt(2, fact.f_d_id);
			factPrepStmt.addBatch();

		} catch (SQLException se) {
			throw new RuntimeException(se);
		}

	} // end insertOrderLine()

} // end class jdbcIO()
