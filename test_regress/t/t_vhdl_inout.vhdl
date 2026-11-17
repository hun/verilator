-- DESCRIPTION: Verilator: VHDL top-level inout ports with tristate drivers
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    oe  : in std_logic;
    d   : in std_logic;
    dv  : in std_logic_vector(3 downto 0);
    io  : inout std_logic;
    ios : inout std_logic;
    iov : inout std_logic_vector(3 downto 0);
    y   : out std_logic;
    ys  : out std_logic;
    yv  : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  io <= d when oe = '1' else 'Z';
  with oe select ios <= d when '1', 'Z' when others;
  iov <= dv when oe = '1' else (others => 'Z');
  -- The external value is what the model sees on the pin; the module's own
  -- contribution is exposed as the __en/__out ports by --pins-inout-enables
  y <= io;
  ys <= ios;
  yv <= iov;
end;
