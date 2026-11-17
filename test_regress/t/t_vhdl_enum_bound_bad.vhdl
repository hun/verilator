-- DESCRIPTION: Verilator: VHDL out-of-range enumeration attribute
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (o : out bit);
end;

architecture rtl of t is
  type state_t is (idle, run, done);
  constant next_state : state_t := state_t'succ(done);
begin
  o <= '0';
end;
