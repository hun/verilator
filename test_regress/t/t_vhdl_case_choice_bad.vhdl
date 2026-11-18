-- DESCRIPTION: Verilator: VHDL case and selected choice diagnostics
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- Each process below has an illegal choice list (IEEE 1076-2008 10.9, 11.5);
-- GHDL refuses every one of them.

entity t is
  port (
    a : in  bit;
    b : in  bit;
    y : out bit
  );
end;

architecture rtl of t is
  type op_t is (add, sub, mul);
begin
  -- A case over an enumeration that misses one literal
  proc_missing : process (a)
  begin
    case a is
      when '0' => y <= '0';
      when '1' => y <= '1';
    end case;
  end process;

  proc_enum_missing : process (a)
    variable o : op_t;
  begin
    o := add;
    case o is
      when add => y <= '0';
      when sub => y <= '1';
    end case;
  end process;

  -- A duplicate choice
  proc_dup : process (a)
  begin
    case a is
      when '0'        => y <= '0';
      when '0'        => y <= '1';
      when others     => y <= '0';
    end case;
  end process;

  -- Overlapping choices: the range covers 2, which is also chosen alone
  proc_overlap : process (a)
    variable n : integer;
  begin
    n := 0;
    case n is
      when 0 to 3 => y <= '0';
      when 2      => y <= '1';
      when others => y <= '0';
    end case;
  end process;

  -- A second `others` arm can never be selected
  proc_two_others : process (a)
  begin
    case a is
      when '0'    => y <= '0';
      when others => y <= '1';
      when others => y <= '0';
    end case;
  end process;

  -- A selected assignment without `others` that misses a value
  y <= '0' when a = '1' else
       '1';
  sel_dup : with a select
    y <= '0' when '1',
         '1' when '1';
  sel_missing : with a select
    y <= '0' when '1';
end;
