-- DESCRIPTION: Verilator: real variables and signals with arithmetic and conversions
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    i_in     : in  std_logic_vector(7 downto 0);
    o_scaled : out std_logic_vector(31 downto 0);
    o_round  : out std_logic_vector(31 downto 0);
    o_cmp    : out std_logic_vector(3 downto 0);
    o_sig    : out std_logic_vector(31 downto 0)
  );
end entity t;

architecture rtl of t is
  signal sig_r : real;
begin
  -- Driven from an input, so the value is only known at run time
  sig_r <= real(to_integer(unsigned(i_in))) / 4.0;
  o_sig <= std_logic_vector(to_signed(integer(sig_r * 2.0), 32));

  process
    variable r : real;
    variable q : real;
    variable c : std_logic_vector(3 downto 0);
  begin
    r := 2.5;
    q := r * real(4) / 2.0 + 0.25;  -- 5.25
    c := "0000";
    if r < q then c(0) := '1'; end if;
    if r = 2.5 then c(1) := '1'; end if;
    if q >= 5.25 then c(2) := '1'; end if;
    if q /= 5.25 then c(3) := '1'; end if;
    o_cmp <= c;
    -- A conversion to an integer rounds to nearest, halfway away from zero
    o_scaled <= std_logic_vector(to_signed(integer(q), 32));  -- 5.25 -> 5
    o_round <= std_logic_vector(to_signed(integer(-r), 32));  -- -2.5 -> -3
    wait;
  end process;
end architecture rtl;
