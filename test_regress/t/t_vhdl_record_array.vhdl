-- DESCRIPTION: Verilator: a record element of an array local takes an aggregate
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- poc's arith_cca plans its levels in a record of integers held in an array local:
-- `res(i) := (base, core, done)` in a function, returned as `res(0 to i)` and read back
-- through field selection and `'length`. The declared element type keeps the record's
-- fields, so the aggregate fills them instead of covering the packed vector's bits.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port(
    o_base : out std_logic_vector(31 downto 0);
    o_core : out std_logic_vector(31 downto 0);
    o_done : out std_logic_vector(31 downto 0);
    o_len : out std_logic_vector(31 downto 0);
    o_gen : out std_logic_vector(31 downto 0);
    o_sum : out std_logic_vector(31 downto 0)
  );
end entity t;

architecture rtl of t is
  type t_level is record
    base : natural;
    core : integer;
    done : natural;
  end record t_level;
  type t_levels is array (natural range <>) of t_level;

  function compact return t_levels is
    variable res : t_levels(0 to 31);
    variable base, core, done : integer;
  begin
    base := 0;
    core := 3;
    done := 2;
    for i in res'range loop
      res(i) := (base, core, done);
      if core <= 0 then
        return res(0 to i);
      end if;
      base := base + 2 * core;
      done := done + (core - 2 * (core / 2) + 2);
      core := core / 2 - 2;
    end loop;
    return res;
  end function compact;

  -- One element write in a function the concurrent assignment inlines
  function pick(i : natural) return integer is
    variable res : t_levels(0 to 3);
  begin
    res(i) := (7, 8, 9);
    return res(i).base + res(i).core + res(i).done;
  end function pick;

  constant levels : t_levels := compact;
  constant more : boolean := levels'length > 1;
  signal gen_sum : std_logic_vector(31 downto 0);
begin
  o_base <= std_logic_vector(to_unsigned(levels(1).base, 32));
  o_core <= std_logic_vector(to_signed(levels(1).core, 32));
  o_done <= std_logic_vector(to_unsigned(levels(0).done, 32));
  o_len <= std_logic_vector(to_unsigned(levels'length, 32));
  o_sum <= std_logic_vector(to_signed(pick(2), 32));

  gen : if more generate
    gen_sum <= std_logic_vector(to_unsigned(levels(1).done, 32));
  end generate gen;
  o_gen <= gen_sum;
end architecture rtl;
