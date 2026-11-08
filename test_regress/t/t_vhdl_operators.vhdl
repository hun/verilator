-- DESCRIPTION: Verilator: VHDL arithmetic, comparison, logic and shift operators
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a     : in integer;
    b     : in integer;
    u     : in unsigned(3 downto 0);
    s     : in signed(3 downto 0);
    mul_o : out integer;
    div_o : out integer;
    mod_o : out integer;
    rem_o : out integer;
    pow_o : out integer;
    abs_o : out integer;
    neg_o : out integer;
    lt_o  : out std_logic;
    le_o  : out std_logic;
    gt_o  : out std_logic;
    ge_o  : out std_logic;
    and_o : out unsigned(3 downto 0);
    nand_o : out unsigned(3 downto 0);
    nor_o : out unsigned(3 downto 0);
    xnor_o : out unsigned(3 downto 0);
    sll_o : out unsigned(3 downto 0);
    srl_o : out unsigned(3 downto 0);
    sra_o : out unsigned(3 downto 0);
    ssll_o : out signed(3 downto 0);
    ssrl_o : out signed(3 downto 0);
    ssla_o : out signed(3 downto 0);
    ssra_o : out signed(3 downto 0);
    slt_o : out std_logic
  );
end;

architecture rtl of t is
begin
  mul_o <= a * b;
  div_o <= a / b;
  mod_o <= a mod b;
  rem_o <= a rem b;
  pow_o <= a ** 2;
  abs_o <= abs a;
  neg_o <= -a - (b - 1);

  lt_o <= '1' when a < b else '0';
  le_o <= '1' when a <= b else '0';
  gt_o <= '1' when a > b else '0';
  ge_o <= '1' when a >= b else '0';

  and_o <= u and (u - 1);
  nand_o <= u nand (u - 1);
  nor_o <= u nor (u - 1);
  xnor_o <= u xnor (u - 1);

  sll_o <= u sll 1;
  srl_o <= u srl 2;
  sra_o <= u sra 1;
  ssll_o <= s sll 1;
  ssrl_o <= s srl 1;
  ssla_o <= s sla 1;
  ssra_o <= s sra 1;
  slt_o <= '1' when s < 0 else '0';
end;
