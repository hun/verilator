-- DESCRIPTION: Verilator: real elements and record fields of constant aggregates
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package real_agg_pkg is
  type real_vec_t is array (natural range <>) of real;
  type stall_t is record
    stall_probability : real;
    count : integer;
    scale : real;
  end record;
  subtype time_r is real range real'low to real'high;
  type time_vec_t is array (natural range <>) of time_r;
  constant times_s : real_vec_t(0 to 1) := (1.25e-9, 3.0 * 60.0e1);
  constant res : time_vec_t(0 to 2) := (1.0e-9, 1.0e-6, 1.0e-3);
  constant no_stall : stall_t := (stall_probability => 0.0, count => 3, scale => -2.5);
  constant stalled : stall_t := (stall_probability => 0.75, count => 7, scale => 1.0);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.real_agg_pkg.all;

entity t is
end entity;

architecture test of t is
begin
  process
  begin
    assert integer(times_s(1)) = 1800 report "times_s(1)" severity failure;
    assert integer(times_s(0) * 1.0e9) = 1 report "times_s(0)" severity failure;
    assert times_s(0) < times_s(1) report "order" severity failure;
    assert no_stall.stall_probability = 0.0 report "stall_probability" severity failure;
    assert no_stall.count = 3 report "count" severity failure;
    assert integer(no_stall.scale) = -3 report "scale rounds away from zero" severity failure;
    assert stalled.stall_probability > no_stall.stall_probability report "compare" severity failure;
    assert integer(stalled.stall_probability * 4.0) + stalled.count = 10 report "mixed" severity failure;
    assert integer(res(2) * 1.0e6) = 1000 report "res(2)" severity failure;
    report "all real aggregates match";
    wait;
  end process;
end architecture;
