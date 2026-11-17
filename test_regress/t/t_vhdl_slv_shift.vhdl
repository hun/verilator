-- DESCRIPTION: Verilator: VHDL predefined and numeric_std shift fill
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    sv    : in std_logic_vector(3 downto 0);
    u     : in unsigned(3 downto 0);
    n     : in integer range 0 to 5;
    sv_sra : out std_logic_vector(3 downto 0);
    sv_sla : out std_logic_vector(3 downto 0);
    u_sra  : out unsigned(3 downto 0);
    u_sla  : out unsigned(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- Predefined arrays fill from the edge; numeric_std unsigned shifts in zeros
  sv_sra <= sv sra n;
  sv_sla <= sv sla n;
  u_sra <= u sra n;
  u_sla <= u sla n;
end;
