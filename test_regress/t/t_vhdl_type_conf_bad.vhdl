-- DESCRIPTION: Verilator: VHDL assignment, return, call, generic and conversion type conformance
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- Every statement below associates a value with an object of an unrelated base
-- type, or with a composite of a different length. GHDL and nvc refuse every
-- one of them: VHDL has no implicit conversion except a universal numeric
-- literal. The frontend must report each at its source line.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity child is
  generic (W : integer := 8);
  port (y : out integer);
end entity;

architecture rtl of child is
begin
  y <= 0;
end architecture;

entity t is
  port (
    clk    : in  std_logic;
    en     : in  std_logic;
    sel    : in  std_logic;
    a4     : in  std_logic_vector(3 downto 0);
    b      : in  boolean;
    o_len  : out std_logic_vector(7 downto 0);
    o_int  : out std_logic;
    o_vec  : out integer;
    o_un   : out unsigned(7 downto 0);
    o_bool : out std_logic;
    o_ret  : out std_logic;
    o_call : out std_logic;
    o_conv : out std_logic_vector(7 downto 0);
    o_cvi  : out integer
  );
end entity;

architecture rtl of t is
  type a_t is (ea0, ea1);
  type b_t is (eb0, eb1);
  signal lit_a : a_t;
  signal int_s : integer;
  constant text_c : string := "ab";
  signal slv8 : std_logic_vector(7 downto 0);
  constant bad_const : std_logic := 42;
  constant bad_time : time := time(5);
  signal bad_init : std_logic := 42;

  function bad_return return std_logic is
  begin
    return 1;
  end function;

  function ident(x : std_logic) return std_logic is
  begin
    return x;
  end function;
begin
  slv8 <= a4 & a4;

  -- A value of the same base type with a different length
  o_len <= a4;
  lit_a <= eb0;
  -- An integer literal and an object of an unrelated base type
  o_int <= 42;
  o_vec <= a4;
  o_un  <= slv8;
  o_bool <= b;
  o_bool <= sel and b;
  o_bool <= not int_s;
  o_bool <= not text_c;
  o_bool <= sel = int_s;
  o_bool <= b = sel;
  o_bool <= slv8 = b;
  o_bool <= slv8 = sel;

  -- A declared return type does not convert the returned value
  o_ret <= bad_return;
  -- A single visible declaration is not a reason to skip conformance
  o_call <= ident(5);

  -- A type conversion needs closely related types
  o_conv <= std_logic_vector(42);
  o_cvi <= integer(a4);

  process (clk)
    variable v : std_logic;
  begin
    v := 42;
  end process;

  -- Both the conditional and the selected form check every value
  o_len <= '0' & slv8(6 downto 0) when en = '1' else 42;
  with sel select o_int <= '0' when '0', 42 when others;

  u_child : entity work.child generic map (W => "abc") port map (y => open);
end architecture;
