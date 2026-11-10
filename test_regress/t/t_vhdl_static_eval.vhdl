-- DESCRIPTION: Verilator: VHDL static function evaluation and subtype expressions
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity drv is
  generic (
    val : string
  );
  port (
    o : out std_logic
  );
end;

architecture rtl of drv is
begin
  gen1: if val = "one" generate
    o <= '1';
  end generate;

  gen0: if val /= "one" generate
    o <= '0';
  end generate;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a      : in  std_logic_vector(7 downto 0);
    y      : out std_logic_vector(3 downto 0);
    z      : out std_logic_vector(4 downto 0);
    hi     : out integer;
    sel    : out std_logic;
    strout : out std_logic
  );
end;

architecture rtl of t is
  function log2n(v : natural) return natural is
    variable r : natural := 0;
    variable x : natural := v;
  begin
    while x > 1 loop
      r := r + 1;
      x := x / 2;
    end loop;
    return r;
  end log2n;

  constant W : natural := log2n(16);
  subtype idx_t is natural range 7 downto 4;
  subtype slice_t is natural range idx_t'low downto 0;
  constant S0 : std_logic_vector(1 downto 0) := '0' & '0';
  constant S1 : std_logic_vector(1 downto 0) := '0' & '1';
  constant S2 : std_logic_vector(1 downto 0) := '1' & '0';
  constant S3 : std_logic_vector(1 downto 0) := '1' & '1';
begin
  y <= a(W - 1 downto 0);
  z <= a(slice_t);
  hi <= idx_t'high;

  with a(1 downto 0) select
    sel <= '1' when S1,
           '0' when others;

  d1: entity work.drv
    generic map (val => "one")
    port map (o => strout);
end;
