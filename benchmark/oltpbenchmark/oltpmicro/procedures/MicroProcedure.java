package com.oltpbenchmark.benchmarks.micro.procedures;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Random;

import com.oltpbenchmark.api.Procedure;
import com.oltpbenchmark.benchmarks.micro.MicroWorker;

public abstract class MicroProcedure extends Procedure{

    public abstract ResultSet run(Connection conn, Random gen,
            int scaleFactor, MicroWorker w) throws SQLException;

}
