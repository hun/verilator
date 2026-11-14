-- DESCRIPTION: Verilator: a procedure with signal formals writes a variable
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    BusA   : in  std_logic_vector(7 downto 0);
    BusB   : in  std_logic_vector(7 downto 0);
    ALU_Op : in  std_logic_vector(3 downto 0);
    F_In   : in  std_logic_vector(7 downto 0);
    Q      : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
  procedure AddSub(A        : std_logic_vector;
                   B        : std_logic_vector;
                   Sub      : std_logic;
                   Carry_In : std_logic;
                   signal Res   : out std_logic_vector;
                   signal Carry : out std_logic) is

    variable B_i   : unsigned(A'length - 1 downto 0);
    variable Res_i : unsigned(A'length + 1 downto 0);
  begin
    if Sub = '1' then
      B_i := not unsigned(B);
    else
      B_i :=     unsigned(B);
    end if;

    Res_i := unsigned("0" & A & Carry_In) + unsigned("0" & B_i & "1");
    Carry <= Res_i(A'length + 1);
    Res <= std_logic_vector(Res_i(A'length downto 1));
  end procedure AddSub;

  signal UseCarry : std_logic;
  signal HalfCarry_v : std_logic;
  signal Carry7_v : std_logic;
  signal Q_v : std_logic_vector(7 downto 0);
begin
  UseCarry <= not ALU_Op(2) and ALU_Op(0);
  AddSub(BusA(3 downto 0), BusB(3 downto 0), ALU_Op(1),
         ALU_Op(1) xor (UseCarry and F_In(5)), Q_v(3 downto 0), HalfCarry_v);
  AddSub(BusA(6 downto 4), BusB(6 downto 4), ALU_Op(1), HalfCarry_v, Q_v(6 downto 4), Carry7_v);
  Q <= Q_v;
end architecture rtl;
