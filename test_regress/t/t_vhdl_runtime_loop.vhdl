-- DESCRIPTION: Verilator: a run-time loop with exit, a slice with a run-time width, and a
-- conversion whose width is a run-time value
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- open-logic's first-bit decoder searches a variable number of bits per stage, leaves its
-- search with `exit`, writes the found index into a slice whose bounds are variables, and
-- converts an integer to a vector whose length is one of those variables. Each of those is
-- derived from the input, so the loop count, the slice width and the conversion width are
-- all run-time values.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk  : in  std_logic;
    din  : in  std_logic_vector(7 downto 0);
    found : out std_logic;
    addr  : out std_logic_vector(2 downto 0);
    sliced : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
begin
  p : process (clk)
    variable first_v : std_logic;
    variable addr_v  : std_logic_vector(2 downto 0);
    variable slice_v : std_logic_vector(7 downto 0);
    variable count_v : natural;
    variable hi_v    : natural;
    constant pattern_c : unsigned(3 downto 0) := "0011";
  begin
    if rising_edge(clk) then
      first_v := '0';
      addr_v := (others => '0');
      count_v := din'length;
      for i in 0 to count_v - 1 loop
        if din(i) = '1' then
          addr_v := std_logic_vector(to_unsigned(i, 3));
          first_v := '1';
          exit;
        end if;
      end loop;

      -- A slice whose width comes from the input, written from a conversion whose length
      -- is that same run-time value
      hi_v := to_integer(unsigned(din(1 downto 0)));
      slice_v := (others => '0');
      if hi_v < 8 then
        slice_v(hi_v downto 0) := std_logic_vector(to_unsigned(3, hi_v + 1));
      end if;

      found <= first_v;
      addr <= addr_v;
      sliced <= slice_v;
    end if;
  end process;
end architecture;
