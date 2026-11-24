-- DESCRIPTION: Verilator: finish and stop of std.env, called by their bare names
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

use std.env.all;

entity t_vhdl_env_finish_stop is
  port (f : in bit; s : in bit);
end;

architecture r of t_vhdl_env_finish_stop is
begin
  process (f)
  begin
    if f = '1' then
      finish(0);
    end if;
  end process;

  process (s)
  begin
    if s = '1' then
      stop;
    end if;
  end process;
end;
