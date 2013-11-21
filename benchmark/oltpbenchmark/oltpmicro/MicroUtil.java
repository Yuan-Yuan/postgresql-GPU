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
 * jMicroUtil - utility functions for the Open Source Java implementation of 
 *    the TPC-C benchmark
 *
 */

import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.dateFormat;
import static com.oltpbenchmark.benchmarks.micro.jMicroConfig.nameTokens;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Random;

import com.oltpbenchmark.benchmarks.tpcc.pojo.Customer;
import com.oltpbenchmark.util.RandomGenerator;

public class MicroUtil {

    /**
     * Creates a Customer object from the current row in the given ResultSet.
     * The caller is responsible for closing the ResultSet.
     * @param rs an open ResultSet positioned to the desired row
     * @return the newly created Customer object
     * @throws SQLException for problems getting data from row
     */

	private static final RandomGenerator ran = new RandomGenerator(0);

	public static String randomStr(int strLen) {
	    if (strLen > 1) 
	        return ran.astring(strLen - 1, strLen - 1);
	    else
	        return "";
	} 

	public static String randomNStr(int stringLength) {
		if (stringLength > 0)
		    return ran.nstring(stringLength, stringLength);
		else
		    return "";
	}

	public static String getCurrentTime() {
		return dateFormat.format(new java.util.Date());
	}

	public static String formattedDouble(double d) {
		String dS = "" + d;
		return dS.length() > 6 ? dS.substring(0, 6) : dS;
	}

	private static final int OL_I_ID_C = 7911; // in range [0, 8191]
	private static final int C_ID_C = 259; // in range [0, 1023]
	private static final int C_LAST_LOAD_C = 157; // in range [0, 255]
	private static final int C_LAST_RUN_C = 223; // in range [0, 255]

	public static int getDimID(Random r) {
		return nonUniformRandom(8191, OL_I_ID_C, 1, 100000, r);
	}

	public static int getFactID(Random r) {
		return nonUniformRandom(1023, C_ID_C, 1, 3000, r);
	}

	public static String getLastName(int num) {
		return nameTokens[num / 100] + nameTokens[(num / 10) % 10]
				+ nameTokens[num % 10];
	}

	public static String getNonUniformRandomLastNameForRun(Random r) {
		return getLastName(nonUniformRandom(255, C_LAST_RUN_C, 0, 999, r));
	}

	public static String getNonUniformRandomLastNameForLoad(Random r) {
		return getLastName(nonUniformRandom(255, C_LAST_LOAD_C, 0, 999, r));
	}

	public static int randomNumber(int min, int max, Random r) {
		return (int) (r.nextDouble() * (max - min + 1) + min);
	}

	public static int nonUniformRandom(int A, int C, int min, int max, Random r) {
		return (((randomNumber(0, A, r) | randomNumber(min, max, r)) + C) % (max
				- min + 1))
				+ min;
	}

} 
