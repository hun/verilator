-- DESCRIPTION: Verilator: dynamic VHDL enumeration succ bound check
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package enum_dyn_pkg is
  type state_t is (idle, run, stop);
end package;

use work.enum_dyn_pkg.all;

entity t is
  port (
    e : in  state_t;
    s : out state_t
  );
end;

architecture rtl of t is
begin
  s <= state_t'succ(e);
end;
