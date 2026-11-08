-- DESCRIPTION: Verilator: VHDL user-defined function calls through GHDL XML
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
package fnp is
  type state_t is (idle, run, done);
  type rec_t is record
    en  : std_logic;
    st  : state_t;
    num : std_logic_vector(1 downto 0);
  end record;

  function add1(x : integer) return integer;
  function maxi(x, y : integer) return integer;
  function pick(sel : integer; a : integer := 7; b : integer := 9) return integer;
  function clamp(x : unsigned(3 downto 0)) return unsigned;
  function next_state(s : state_t) return state_t;
  function mk_rec(en : std_logic; num : std_logic_vector(1 downto 0)) return rec_t;
end;

package body fnp is
  function add1(x : integer) return integer is
  begin
    return x + 1;
  end;

  function maxi(x, y : integer) return integer is
    variable r : integer;
  begin
    if x > y then
      r := x;
    else
      r := y;
    end if;
    return r;
  end;

  function pick(sel : integer; a : integer := 7; b : integer := 9) return integer is
    variable r : integer := 0;
    constant k : integer := 2;
  begin
    case sel is
      when 0 => r := a + k;
      when 1 => r := b - k;
      when others => r := a * b;
    end case;
    return r;
  end;

  function clamp(x : unsigned(3 downto 0)) return unsigned is
  begin
    if x > "1000" then
      return "1000";
    end if;
    return x;
  end;

  function next_state(s : state_t) return state_t is
  begin
    case s is
      when idle => return run;
      when run => return done;
      when others => return idle;
    end case;
  end;

  function mk_rec(en : std_logic; num : std_logic_vector(1 downto 0)) return rec_t is
  begin
    return (en => en, st => idle, num => num);
  end;
end;

library ieee;
use ieee.std_logic_1164.all; use ieee.numeric_std.all; use work.fnp.all;

entity t is
  port (
    a : in integer; b : in integer; s : in integer range 0 to 2;
    u : in unsigned(3 downto 0); st_in : in state_t;
    din_en : in std_logic; din_num : in std_logic_vector(1 downto 0);
    y : out integer; v : out unsigned(3 downto 0); w : out integer; nn : out state_t;
    r_en : out std_logic; r_st : out state_t; r_num : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
  function dbl(x : integer) return integer is
  begin
    return x * 2;
  end;
  signal r : rec_t;
begin
  y <= add1(a) + maxi(a, b) + pick(s) + dbl(b);
  v <= clamp(u);
  w <= pick(b => 3, a => 4, sel => 1) + maxi(add1(a), b);
  nn <= next_state(st_in);
  r <= mk_rec(din_en, din_num);
  r_en <= r.en;
  r_st <= r.st;
  r_num <= r.num;
end;
