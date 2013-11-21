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

import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.terminalPrefix;

import java.io.IOException;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;

import org.apache.log4j.Logger;


import com.oltpbenchmark.WorkloadConfiguration;
import com.oltpbenchmark.api.BenchmarkModule;
import com.oltpbenchmark.api.Loader;
import com.oltpbenchmark.api.Worker;
import com.oltpbenchmark.benchmarks.micro.procedures.Update;
import com.oltpbenchmark.util.SimpleSystemPrinter;

public class MicroBenchmark extends BenchmarkModule {
    private static final Logger LOG = Logger.getLogger(MicroBenchmark.class);

	public MicroBenchmark(WorkloadConfiguration workConf) {
		super("micro", workConf, true);
	}

	@Override
	protected Package getProcedurePackageImpl() {
		return (Update.class.getPackage());
	}

	/**
	 * @param Bool
	 */
	@Override
	protected List<Worker> makeWorkersImpl(boolean verbose) throws IOException {

		jMicroConfig.TERMINAL_MESSAGES = false;
		ArrayList<Worker> workers = new ArrayList<Worker>();

		try {
			List<MicroWorker> terminals = createTerminals();
			workers.addAll(terminals);
		} catch (Exception e) {
			e.printStackTrace();
		}

		return workers;
	}

	@Override
	protected Loader makeLoaderImpl(Connection conn) throws SQLException {
		return new MicroLoader(this, conn);
	}

	protected ArrayList<MicroWorker> createTerminals() throws SQLException {

		MicroWorker[] terminals = new MicroWorker[workConf.getTerminals()];

		int scaleFactor = (int) workConf.getScaleFactor();
		int numTerminals = workConf.getTerminals();
		assert (numTerminals >= scaleFactor) :
		    String.format("Insufficient number of terminals '%d' [scaleFactor=%d]",
		                  numTerminals, scaleFactor);

		String[] terminalNames = new String[numTerminals];

		for (int terminalId = 0; terminalId < numTerminals; terminalId++) {

			String terminalName = terminalPrefix + terminalId; 

			MicroWorker terminal = new MicroWorker(terminalName,this,
					new SimpleSystemPrinter(null), new SimpleSystemPrinter(
							System.err), scaleFactor);
			terminals[terminalId] = terminal;
			terminalNames[terminalId] = terminalName;
		}

		assert terminals[terminals.length - 1] != null;

		ArrayList<MicroWorker> ret = new ArrayList<MicroWorker>();
		for (MicroWorker w : terminals)
			ret.add(w);
		return ret;
	}

}
