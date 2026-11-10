-- DESCRIPTION: Verilator: VHDL shared variables
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk    : in std_logic;
    a      : in unsigned(7 downto 0);
    we     : in std_logic;
    waddr  : in unsigned(1 downto 0);
    raddr  : in unsigned(1 downto 0);
    din    : in unsigned(7 downto 0);
    q      : out unsigned(7 downto 0);
    dout   : out unsigned(7 downto 0);
    mix    : out unsigned(7 downto 0);
    etag   : out unsigned(7 downto 0);
    bcount : out unsigned(7 downto 0)
  );
  -- An entity declarative item is visible in every architecture
  shared variable bias : unsigned(7 downto 0) := X"2A";
end;

architecture rtl of t is
  type mem_t is array (0 to 3) of unsigned(7 downto 0);
  shared variable acc : unsigned(7 downto 0) := (others => '0');
  shared variable mem : mem_t := (others => (others => '0'));
begin
  -- Shared variables are variables: assignment is immediate, and they are
  -- visible to every process in the architecture.
  process (clk) is
  begin
    if rising_edge(clk) then
      acc := acc + a;
      q <= acc;
      if we = '1' then
        mem(to_integer(waddr)) := din;
      end if;
    end if;
  end process;

  process (clk) is
  begin
    if rising_edge(clk) then
      dout <= mem(to_integer(raddr));
      etag <= bias;
    end if;
  end process;

  -- A shared variable in a block declarative part
  blk : block
    shared variable bv : unsigned(7 downto 0) := X"01";
  begin
    process (clk) is
    begin
      if rising_edge(clk) then
        bv := bv + 1;
        bcount <= bv;
      end if;
    end process;
  end block;

  -- A shared variable is not a signal, so process(all) is not sensitive to it
  process (all) is
  begin
    mix <= acc xor a;
  end process;
end;
